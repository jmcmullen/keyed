import { drizzle } from "drizzle-orm/expo-sqlite";
import { deleteDatabaseSync, openDatabaseSync } from "expo-sqlite";
import { log } from "./log";
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
		log.error(
			{
				action: "db.reset.close.error",
				surface: "db-client",
				database: DATABASE,
			},
			err,
		);
	}
	try {
		deleteDatabaseSync(DATABASE);
	} catch (err: unknown) {
		log.error(
			{
				action: "db.reset.delete.error",
				surface: "db-client",
				database: DATABASE,
			},
			err,
		);
	}
	client = open();
	db = client.db;
}
