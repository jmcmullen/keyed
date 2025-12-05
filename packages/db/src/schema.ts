import { integer, real, sqliteTable, text } from "drizzle-orm/sqlite-core";

export const detections = sqliteTable("detections", {
	id: text("id").primaryKey(),
	bpm: integer("bpm").notNull(),
	bpmConfidence: real("bpm_confidence").notNull(),
	key: text("key").notNull(),
	keyConfidence: real("key_confidence").notNull(),
	camelotCode: text("camelot_code").notNull(),
	duration: integer("duration").notNull(),
	createdAt: integer("created_at", { mode: "timestamp" }).notNull(),
});

export type Detection = typeof detections.$inferSelect;
export type NewDetection = Omit<typeof detections.$inferInsert, "id">;
