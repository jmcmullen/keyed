import { desc, eq, inArray } from "drizzle-orm";
import { useMigrations } from "drizzle-orm/expo-sqlite/migrator";
import { randomUUID } from "expo-crypto";
import {
	createContext,
	useCallback,
	useContext,
	useEffect,
	useState,
} from "react";
import { Text, View } from "react-native";
import migrations from "../drizzle/migrations";
import { db, resetDatabase } from "./client";
import { type Detection, detections, type NewDetection } from "./schema";

interface DbContextValue {
	detections: Detection[];
	addDetection: (data: NewDetection) => Promise<void>;
	deleteDetection: (id: string) => Promise<void>;
	clearHistory: () => Promise<void>;
}

const DbContext = createContext<DbContextValue | null>(null);
const LIMIT = 2_000;

export function DbProvider({ children }: { children: React.ReactNode }) {
	const [attempt, setAttempt] = useState(0);
	const [wiped, setWiped] = useState(false);
	const reset = useCallback(() => {
		setWiped(true);
		setAttempt((value) => value + 1);
	}, []);

	return (
		<DbProviderAttempt key={attempt} wiped={wiped} reset={reset}>
			{children}
		</DbProviderAttempt>
	);
}

function DbProviderAttempt({
	children,
	wiped,
	reset,
}: {
	children: React.ReactNode;
	wiped: boolean;
	reset: () => void;
}) {
	const { success: isReady, error } = useMigrations(db, migrations);

	useEffect(() => {
		if (!error) return;
		console.error("[db] migration failed", error);
		if (wiped) return;
		resetDatabase();
		reset();
	}, [error, reset, wiped]);

	if (!isReady && !error) {
		return (
			<View style={{ flex: 1, justifyContent: "center", alignItems: "center" }}>
				<Text style={{ color: "#888888" }}>Loading...</Text>
			</View>
		);
	}

	if (error && !wiped) {
		return (
			<View style={{ flex: 1, justifyContent: "center", alignItems: "center" }}>
				<Text style={{ color: "#888888" }}>Resetting database...</Text>
			</View>
		);
	}

	if (error) {
		return (
			<View style={{ flex: 1, justifyContent: "center", alignItems: "center" }}>
				<Text style={{ color: "#ff453a" }}>Database setup failed</Text>
			</View>
		);
	}

	return <DbProviderInner recover={reset}>{children}</DbProviderInner>;
}

function DbProviderInner({
	children,
	recover,
}: {
	children: React.ReactNode;
	recover: () => void;
}) {
	const [detectionsData, setDetectionsData] = useState<Detection[]>([]);

	const log = useCallback((message: string, err: unknown): void => {
		console.error(`[db] ${message}`, err);
	}, []);

	const fail = useCallback(
		(message: string, err: unknown): void => {
			log(message, err);
			resetDatabase();
			recover();
		},
		[log, recover],
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
			fail("refetch failed", err);
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
				fail("add detection failed", err);
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
				fail("delete detection failed", err);
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
			fail("clear history failed", err);
			throw err;
		}
	}, [fail]);

	return (
		<DbContext.Provider
			value={{
				detections: detectionsData,
				addDetection,
				deleteDetection,
				clearHistory,
			}}
		>
			{children}
		</DbContext.Provider>
	);
}

export function useDb(): DbContextValue {
	const context = useContext(DbContext);
	if (!context) {
		throw new Error("useDb must be used within a DbProvider");
	}
	return context;
}
