"""Processor registry with decorator-based registration.

OCP: New processors register themselves via @register_processor decorator.
Adding a new processor never requires modifying this module or the factory.
To add a new processor:
    1. Create a new file (e.g., new_processor.py)
    2. Decorate the class with @register_processor(ProcessingType.NEW_TYPE)
    3. Import the module in processing/__init__.py
That's it - no switch/case to modify.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from see_sharp.config import config_keys
from see_sharp.model.general_params import GeneralParams, ProcessingType

if TYPE_CHECKING:
    from see_sharp.processing.frame_processor import FrameProcessor

_REGISTRY: dict[ProcessingType, type[FrameProcessor]] = {}


def register_processor(processing_type: ProcessingType):
    """Class decorator that registers a processor for a given ProcessingType.

    Usage:
        @register_processor(ProcessingType.LINE_DETECTION)
        class LineProcessor(FrameProcessor):
            ...

    Args:
        processing_type: The ProcessingType this processor handles.

    Returns:
        A decorator that registers the class and returns it unchanged.
    """

    def decorator(cls: type[FrameProcessor]) -> type[FrameProcessor]:
        if processing_type in _REGISTRY:
            raise ValueError(
                f"{processing_type} is already registered to {_REGISTRY[processing_type].__name__}"
            )
        _REGISTRY[processing_type] = cls
        return cls

    return decorator


def create_processor(config: dict) -> FrameProcessor:
    """Create a processor instance based on the config's processing_mode.

    Reads general_params.processing_mode from config, looks up the
    registered processor class, and calls its from_config() factory method.

    Args:
        config: The complete application configuration dictionary.

    Returns:
        A configured FrameProcessor instance.

    Raises:
        ValueError: If no processor is registered for the configured processing type.
    """
    general = GeneralParams.from_config(config[config_keys.GENERAL_PARAMS])
    processor_cls = _REGISTRY.get(general.processing_type)

    if processor_cls is None:
        registered = [pt.value for pt in _REGISTRY]
        raise ValueError(
            f"No processor registered for '{general.processing_type.value}'. "
            f"Available: {registered}"
        )

    return processor_cls.from_config(config)
