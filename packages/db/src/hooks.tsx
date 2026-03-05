import { desc, eq } from "drizzle-orm";
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
import { db } from "./client";
import { type Detection, detections, type NewDetection } from "./schema";

interface DbContextValue {
	detections: Detection[];
	error: string | null;
	addDetection: (data: NewDetection) => Promise<void>;
	deleteDetection: (id: string) => Promise<void>;
	clearHistory: () => Promise<void>;
}

const DbContext = createContext<DbContextValue | null>(null);

export function DbProvider({ children }: { children: React.ReactNode }) {
	const { success: isReady, error } = useMigrations(db, migrations);

	if (error) {
		return (
			<View style={{ flex: 1, justifyContent: "center", alignItems: "center" }}>
				<Text style={{ color: "#EF4444" }}>
					Database error: {error.message}
				</Text>
			</View>
		);
	}

	if (!isReady) {
		return (
			<View style={{ flex: 1, justifyContent: "center", alignItems: "center" }}>
				<Text style={{ color: "#888888" }}>Loading...</Text>
			</View>
		);
	}

	return <DbProviderInner>{children}</DbProviderInner>;
}

function DbProviderInner({ children }: { children: React.ReactNode }) {
	const [detectionsData, setDetectionsData] = useState<Detection[]>([]);
	const [error, setError] = useState<string | null>(null);

	const getErr = useCallback((value: unknown): string => {
		if (value instanceof Error) {
			return value.message;
		}
		return "Database action failed";
	}, []);

	const refetch = useCallback(async (): Promise<void> => {
		const result = await db
			.select()
			.from(detections)
			.orderBy(desc(detections.createdAt));
		setError(null);
		setDetectionsData(result);
	}, []);

	useEffect(() => {
		void refetch().catch((err: unknown) => {
			setError(getErr(err));
		});
	}, [refetch, getErr]);

	const addDetection = useCallback(
		async (newDetection: NewDetection): Promise<void> => {
			const row: Detection = {
				...newDetection,
				id: randomUUID(),
			};
			await db.insert(detections).values(row);
			setError(null);
			setDetectionsData((list) =>
				[row, ...list.filter((item) => item.id !== row.id)].sort(
					(a, b) => b.createdAt.getTime() - a.createdAt.getTime(),
				),
			);
		},
		[],
	);

	const deleteDetection = useCallback(async (id: string): Promise<void> => {
		await db.delete(detections).where(eq(detections.id, id));
		setError(null);
		setDetectionsData((list) => list.filter((item) => item.id !== id));
	}, []);

	const clearHistory = useCallback(async (): Promise<void> => {
		await db.delete(detections);
		setError(null);
		setDetectionsData([]);
	}, []);

	return (
		<DbContext.Provider
			value={{
				detections: detectionsData,
				error,
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
