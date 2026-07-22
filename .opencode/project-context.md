## Project Context: EVE Open Source Client

**Root:** `C:\Users\Metal\Desktop\eve client`

This is an EVE Online open source client project.

### Session Start — Auto-Load

At the start of every session, immediately query the knowledge base to load full project context:

```sql
-- Project metadata
SELECT * FROM v_projects_full WHERE slug='eve-client';

-- File inventory summary
SELECT extension, COUNT(*) as count FROM v_files_full WHERE project_name='EVE Open Source Client' AND is_directory=0 GROUP BY extension ORDER BY count DESC;

-- Key files (non-directory, non-hidden)
SELECT file_name, relative_path, file_size, last_modified FROM v_files_full WHERE project_name='EVE Open Source Client' AND is_directory=0 ORDER BY file_size DESC LIMIT 30;
```

### Query Examples

```
sqlite3 "C:\Users\Metal\Desktop\DB\knowledge_base.db" "SELECT * FROM v_projects_full WHERE slug='eve-client';"
```
