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
	addDetection: (data: NewDetection) => Promise<void>;
	deleteDetection: (id: string) => Promise<void>;
	clearHistory: () => Promise<void>;
}

const DbContext = createContext<DbContextValue | null>(null);

export function DbProvider({ children }: { children: React.ReactNode }) {
	const { success, error } = useMigrations(db, migrations);
	const [isReady, setIsReady] = useState(false);

	useEffect(() => {
		if (success) {
			setIsReady(true);
		}
	}, [success]);

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
	const [data, setData] = useState<Detection[]>([]);

	const refetch = useCallback(async () => {
		const result = await db
			.select()
			.from(detections)
			.orderBy(desc(detections.createdAt));
		setData(result);
	}, []);

	useEffect(() => {
		refetch();
	}, [refetch]);

	const addDetection = useCallback(
		async (newDetection: NewDetection) => {
			await db.insert(detections).values({
				...newDetection,
				id: randomUUID(),
			});
			await refetch();
		},
		[refetch],
	);

	const deleteDetection = useCallback(
		async (id: string) => {
			await db.delete(detections).where(eq(detections.id, id));
			await refetch();
		},
		[refetch],
	);

	const clearHistory = useCallback(async () => {
		await db.delete(detections);
		await refetch();
	}, [refetch]);

	return (
		<DbContext.Provider
			value={{
				detections: data,
				addDetection,
				deleteDetection,
				clearHistory,
			}}
		>
			{children}
		</DbContext.Provider>
	);
}

export function useDb() {
	const context = useContext(DbContext);
	if (!context) {
		throw new Error("useDb must be used within a DbProvider");
	}
	return context;
}
