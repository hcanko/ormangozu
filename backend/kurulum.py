"""Non-destructive local database setup for the v0.6 pilot.

Never delete or reinitialize an operator database during firmware/software upgrades.
Use a backed-up copy and explicit migrations for changes beyond additive columns.
"""
from models import orm  # noqa: F401  (register all SQLAlchemy tables)
from models.database import Base, engine, migrate_sqlite_schema


def setup_database() -> None:
    Base.metadata.create_all(bind=engine)
    migrate_sqlite_schema()
    print('Orman Gözü: tablolar kontrol edildi; mevcut kayıtlar silinmedi.')


if __name__ == '__main__':
    setup_database()
