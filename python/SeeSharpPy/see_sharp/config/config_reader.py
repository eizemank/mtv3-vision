"""Configuration reader - loads JSON config from file.

SRP: This class has a single responsibility - loading JSON from disk.
It does not parse, validate, or map config values to domain objects.
"""

import json
from pathlib import Path


class ConfigReader:
    """Loads and provides raw JSON configuration data."""

    def __init__(self) -> None:
        self._config: dict = {}

    def load_from_file(self, filepath: str) -> dict:
        """Load configuration from a JSON file.

        Args:
            filepath: Path to the JSON config file.

        Returns:
            The parsed configuration dictionary.

        Raises:
            FileNotFoundError: If the config file does not exist.
            json.JSONDecodeError: If the file contains invalid JSON.
        """
        path = Path(filepath)
        with path.open("r", encoding="utf-8") as f:
            self._config = json.load(f)
        return self._config

    @property
    def raw_config(self) -> dict:
        """Access the loaded raw configuration dictionary."""
        return self._config
