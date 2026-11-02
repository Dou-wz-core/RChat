-- Only for a LOCAL copy of the old two-column OfflineMessage table.
-- Inspect the schema first; do not run if id already exists.
ALTER TABLE OfflineMessage ADD COLUMN id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;
CREATE INDEX inbox ON OfflineMessage(userid, id);
