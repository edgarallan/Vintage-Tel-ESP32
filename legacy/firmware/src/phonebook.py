"""
phonebook.py — Rubrica con SQLite

Conserva nome → numero e permette lookup inverso quando arriva chiamata.
"""

import logging
import sqlite3
from contextlib import closing
from pathlib import Path


log = logging.getLogger("phonebook")


class Phonebook:
    def __init__(self, config: dict):
        self.config = config
        self.db_path = Path(config["db_path"])
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self._init_db()

    def _init_db(self):
        with closing(sqlite3.connect(self.db_path)) as conn:
            conn.execute("""
                CREATE TABLE IF NOT EXISTS contacts (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    name TEXT NOT NULL,
                    number TEXT NOT NULL UNIQUE,
                    quick_dial INTEGER UNIQUE
                )
            """)
            conn.commit()
        log.info("Rubrica inizializzata: %s", self.db_path)

    def add(self, name: str, number: str, quick_dial: int | None = None):
        with closing(sqlite3.connect(self.db_path)) as conn:
            try:
                conn.execute(
                    "INSERT INTO contacts (name, number, quick_dial) VALUES (?, ?, ?)",
                    (name, number, quick_dial),
                )
                conn.commit()
                log.info("Aggiunto contatto: %s → %s", name, number)
            except sqlite3.IntegrityError as e:
                log.warning("Duplicato: %s", e)

    def lookup(self, number: str) -> str | None:
        """Risolve un numero in nome. Confronto fuzzy sugli ultimi 9 digit."""
        normalized = "".join(c for c in number if c.isdigit())[-9:]
        if not normalized:
            return None
        with closing(sqlite3.connect(self.db_path)) as conn:
            row = conn.execute(
                "SELECT name FROM contacts WHERE number LIKE ?",
                (f"%{normalized}",),
            ).fetchone()
        return row[0] if row else None

    def quick_dial(self, digit: int) -> str | None:
        """Lookup quick-dial. Prima controlla DB, poi config."""
        with closing(sqlite3.connect(self.db_path)) as conn:
            row = conn.execute(
                "SELECT number FROM contacts WHERE quick_dial = ?",
                (digit,),
            ).fetchone()
        if row:
            return row[0]
        return self.config.get("quick_dial", {}).get(digit)

    def all_contacts(self) -> list[dict]:
        with closing(sqlite3.connect(self.db_path)) as conn:
            conn.row_factory = sqlite3.Row
            rows = conn.execute("SELECT * FROM contacts ORDER BY name").fetchall()
        return [dict(r) for r in rows]
