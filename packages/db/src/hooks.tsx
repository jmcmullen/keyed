import { desc, eq, inArray } from "drizzle-orm";
import { migrate } from "drizzle-orm/expo-sqlite/migrator";
import { randomUUID } from "expo-crypto";
import {
	createContext,
	use,
	useCallback,
	useEffect,
	useMemo,
	useReducer,
	useState,
} from "react";
import { Text, View } from "react-native";
import migrations from "../drizzle/migrations";
import { db, resetDatabase } from "./client";
import { log } from "./log";
import { type Detection, detections, type NewDetection } from "./schema";

interface DbContextValue {
	detections: Detection[];
	addDetection: (data: NewDetection) => Promise<void>;
	deleteDetection: (id: string) => Promise<void>;
	clearHistory: () => Promise<void>;
}

const DbContext = createContext<DbContextValue | null>(null);
const LIMIT = 2_000;

type Migration = {
	attempt: number;
	status: "loading" | "resetting" | "ready" | "failed";
	wiped: boolean;
};

function migration(
	state: Migration,
	action: "start" | "ready" | "retry" | "failed",
): Migration {
	if (action === "start") {
		return { ...state, status: "loading" };
	}
	if (action === "ready") {
		return { ...state, status: "ready" };
	}
	if (action === "retry") {
		return {
			attempt: state.attempt + 1,
			status: "resetting",
			wiped: true,
		};
	}
	return { ...state, status: "failed" };
}

export function DbProvider({ children }: { children: React.ReactNode }) {
	const [state, dispatch] = useReducer(migration, {
		attempt: 0,
		status: "loading",
		wiped: false,
	});
	const recover = useCallback(() => {
		dispatch("retry");
	}, []);

	useEffect(() => {
		let live = true;

		dispatch("start");
		void migrate(db, migrations)
			.then(() => {
				if (!live) return;
				dispatch("ready");
			})
			.catch((err: unknown) => {
				if (!live) return;
				log.error(
					{
						action: "db.migration.error",
						surface: "db-provider",
						attempt: state.attempt,
						wiped: state.wiped,
					},
					err,
				);
				if (state.wiped) {
					dispatch("failed");
					return;
				}
				resetDatabase();
				recover();
			});

		return () => {
			live = false;
		};
	}, [recover, state.attempt, state.wiped]);

	if (state.status === "loading") {
		return (
			<View style={{ flex: 1, justifyContent: "center", alignItems: "center" }}>
				<Text style={{ color: "#888888" }}>Loading...</Text>
			</View>
		);
	}

	if (state.status === "resetting") {
		return (
			<View style={{ flex: 1, justifyContent: "center", alignItems: "center" }}>
				<Text style={{ color: "#888888" }}>Resetting database...</Text>
			</View>
		);
	}

	if (state.status === "failed") {
		return (
			<View style={{ flex: 1, justifyContent: "center", alignItems: "center" }}>
				<Text style={{ color: "#ff453a" }}>Database setup failed</Text>
			</View>
		);
	}

	return <DbProviderInner recover={recover}>{children}</DbProviderInner>;
}

function DbProviderInner({
	children,
	recover,
}: {
	children: React.ReactNode;
	recover: () => void;
}) {
	const [detectionsData, setDetectionsData] = useState<Detection[]>([]);

	const fail = useCallback(
		(action: string, err: unknown): void => {
			log.error(
				{
					action,
					surface: "db-provider",
				},
				err,
			);
			resetDatabase();
			recover();
		},
		[recover],
	);

	const refetch = useCallback(async (): Promise<void> => {
		const result = await db
			.select()
			.from(detections)
			.orderBy(desc(detections.createdAt), desc(detections.id))
			.limit(LIMIT);
		setDetectionsData(result);
	}, []);

	useEffect(() => {
		void refetch().catch((err: unknown) => {
			fail("db.detection_refetch.error", err);
		});
	}, [refetch, fail]);

	const addDetection = useCallback(
		async (newDetection: NewDetection): Promise<void> => {
			const row: Detection = {
				...newDetection,
				id: randomUUID(),
			};
			try {
				db.transaction((tx) => {
					tx.insert(detections).values(row).run();
					const rows = tx
						.select({ id: detections.id })
						.from(detections)
						.orderBy(desc(detections.createdAt), desc(detections.id))
						.all();
					const ids = rows.slice(LIMIT).map((item) => item.id);
					if (ids.length === 0) return;
					tx.delete(detections).where(inArray(detections.id, ids)).run();
				});
				setDetectionsData((list) =>
					[row, ...list.filter((item) => item.id !== row.id)]
						.sort((a, b) => {
							const delta = b.createdAt.getTime() - a.createdAt.getTime();
							if (delta !== 0) return delta;
							return b.id.localeCompare(a.id);
						})
						.slice(0, LIMIT),
				);
			} catch (err: unknown) {
				fail("db.detection_add.error", err);
				throw err;
			}
		},
		[fail],
	);

	const deleteDetection = useCallback(
		async (id: string): Promise<void> => {
			try {
				await db.delete(detections).where(eq(detections.id, id));
				setDetectionsData((list) => list.filter((item) => item.id !== id));
			} catch (err: unknown) {
				fail("db.detection_delete.error", err);
				throw err;
			}
		},
		[fail],
	);

	const clearHistory = useCallback(async (): Promise<void> => {
		try {
			await db.delete(detections);
			setDetectionsData([]);
		} catch (err: unknown) {
			fail("db.history_clear.error", err);
			throw err;
		}
	}, [fail]);

	const value = useMemo(
		() => ({
			detections: detectionsData,
			addDetection,
			deleteDetection,
			clearHistory,
		}),
		[detectionsData, addDetection, deleteDetection, clearHistory],
	);

	return <DbContext.Provider value={value}>{children}</DbContext.Provider>;
}

export function useDb(): DbContextValue {
	const context = use(DbContext);
	if (!context) {
		throw new Error("useDb must be used within a DbProvider");
	}
	return context;
}
