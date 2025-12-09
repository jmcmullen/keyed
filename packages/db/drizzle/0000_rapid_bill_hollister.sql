CREATE TABLE `detections` (
	`id` text PRIMARY KEY NOT NULL,
	`bpm` integer NOT NULL,
	`bpm_confidence` real NOT NULL,
	`key` text NOT NULL,
	`key_confidence` real NOT NULL,
	`camelot_code` text NOT NULL,
	`duration` integer NOT NULL,
	`created_at` integer NOT NULL
);
