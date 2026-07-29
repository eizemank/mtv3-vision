"""SeeSharp Python - Computer Vision Educational Platform.

This is the composition root (entry point) of the application.
It wires all dependencies together following the Dependency Inversion Principle:
    1. Load config
    2. Create processor via registry (OCP)
    3. Inject processor into ProcessingManager (DIP)
    4. Inject FrameSource and manager into Pipeline (DIP)
    5. Start pipeline

To add a new processor, you never touch this file - just create
a new module with @register_processor and import it in processing/__init__.py.
"""

import logging
import sys
from pathlib import Path

from see_sharp.config.config_reader import ConfigReader

# Import processing package to trigger @register_processor decorators
import see_sharp.processing  # noqa: F401

from see_sharp.processing.processor_registry import create_processor
from see_sharp.processing.processing_manager import ProcessingManager
from see_sharp.pipeline.camera_source import CameraSource
from see_sharp.pipeline.pipeline import Pipeline

# конфиг: единый sw/common/config/config.json; локальный config/ — fallback
_CONFIG_CANDIDATES = [
    Path(__file__).resolve().parent.parent.parent / "common" / "config" / "config.json",
    Path(__file__).parent / "config" / "config.json",
]
CONFIG_PATH = next((p for p in _CONFIG_CANDIDATES if p.exists()), _CONFIG_CANDIDATES[0])

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
)
logger = logging.getLogger(__name__)


def main() -> int:
    """Application entry point."""
    # 1. Load configuration
    reader = ConfigReader()
    try:
        config = reader.load_from_file(str(CONFIG_PATH))
    except (FileNotFoundError, Exception) as e:
        logger.error("Failed to load config file '%s': %s", CONFIG_PATH, e)
        return 1

    # 2. Create processor via registry (OCP: no switch/case)
    try:
        processor = create_processor(config)
    except ValueError as e:
        logger.error("Failed to create processor: %s", e)
        return 1

    # 3. Inject processor into manager (DIP)
    manager = ProcessingManager(processor)

    # 4. Create frame source (DIP: abstract FrameSource)
    source = CameraSource(device=0)
    if not source.is_opened():
        logger.error("Failed to open video source")
        return 1

    # 5. Create and start pipeline (DIP: injected dependencies)
    pipeline = Pipeline(source, manager)
    logger.info("Starting pipeline...")

    try:
        pipeline.start()
    except KeyboardInterrupt:
        logger.info("Interrupted by user")
        pipeline.stop()
    finally:
        source.release()

    logger.info("Pipeline stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
