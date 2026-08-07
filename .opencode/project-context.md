## Project Context: EVE Open Source Client

**Root:** the repository workspace

This is an EVE Online open source client project.

### Universal Standards

This project is governed by the Universal Coding Standard and the
Universal Security Standard:

- Coding: the session-provided Universal Coding Standard
- Security: the session-provided Universal Security Standard

Read both fully at the start of every session before any code work
begins. They apply to all code written, reviewed, or modified.

### Knowledge Base

Persistent SQLite knowledge base is provided by the session environment. Use
the knowledge-base skill for its authorized location and query commands; do
not commit machine-specific paths or database locations to this repository.

### Session Start — Auto-Load

```sql
-- Project metadata
SELECT * FROM v_projects_full WHERE slug='eve-client';

-- Self-playback (last session decisions + recent failures)
SELECT * FROM v_session_playback;

-- File inventory
SELECT extension, COUNT(*) as count FROM v_files_full
WHERE project_name='EVE Open Source Client' AND is_directory=0
GROUP BY extension ORDER BY count DESC;

-- Key files
SELECT file_name, relative_path, file_size, last_modified
FROM v_files_full WHERE project_name='EVE Open Source Client'
AND is_directory=0 ORDER BY file_size DESC LIMIT 30;

-- Recent failures
SELECT key, value, created_at FROM v_failure_retrospection;
```
