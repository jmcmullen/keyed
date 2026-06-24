import { drizzle } from "drizzle-orm/expo-sqlite";
import { deleteDatabaseSync, openDatabaseSync } from "expo-sqlite";
import * as schema from "./schema";

export const DATABASE = "keyed.db";

function open() {
	const expo = openDatabaseSync(DATABASE);
	return {
		expo,
		db: drizzle(expo, { schema }),
	};
}

let client = open();
export let db = client.db;

export function resetDatabase() {
	try {
		client.expo.closeSync();
	} catch (err: unknown) {
		console.error("[db] close before reset failed", err);
	}
	try {
		deleteDatabaseSync(DATABASE);
	} catch (err: unknown) {
		console.error("[db] delete during reset failed", err);
	}
	client = open();
	db = client.db;
}
