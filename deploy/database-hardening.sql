-- Run once as a MySQL administrator after replacing the password.
CREATE USER IF NOT EXISTS 'meeting_app'@'127.0.0.1'
  IDENTIFIED BY 'REPLACE_WITH_A_LONG_RANDOM_PASSWORD';
GRANT SELECT, INSERT, UPDATE ON wechat.t_user TO 'meeting_app'@'127.0.0.1';

-- Registration relies on this constraint to prevent duplicate accounts under races.
-- Remove existing duplicate tel rows before running it.
ALTER TABLE wechat.t_user
  ADD UNIQUE KEY uk_t_user_tel (tel);
