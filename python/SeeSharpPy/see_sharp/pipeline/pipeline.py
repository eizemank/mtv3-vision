"""Video processing pipeline with 3-thread architecture.

SRP: This class has one responsibility - orchestrating the
capture -> process -> display threading pipeline.

DIP: Depends on FrameSource (ABC) and ProcessingManager abstractions,
not on concrete camera or processor implementations.

Ported from C++ main.cpp threading logic, but uses Python's thread-safe
queue.Queue instead of manual mutex/condvar/queue management.
"""

import logging
import queue
import threading

import cv2
import numpy as np

from see_sharp.pipeline.frame_source import FrameSource
from see_sharp.processing.processing_manager import ProcessingManager

logger = logging.getLogger(__name__)

_SENTINEL = object()

QUEUE_MAX_SIZE = 2
QUEUE_TIMEOUT_SEC = 0.1


class Pipeline:
    """Three-thread video processing pipeline.

    Threads:
        1. Capture: reads frames from source into frame_queue.
        2. Processing: processes frames via ProcessingManager.
        3. Display: shows results via cv2.imshow.

    Uses queue.Queue with maxsize for backpressure - if processing
    is slow, capture drops frames instead of filling memory.
    """

    def __init__(self, source: FrameSource, manager: ProcessingManager) -> None:
        """Initialize pipeline with injected dependencies.

        Args:
            source: Frame source to capture from (DIP: abstract).
            manager: Processing manager with injected processor (DIP).
        """
        self._source = source
        self._manager = manager
        self._is_running = threading.Event()
        self._frame_queue: queue.Queue = queue.Queue(maxsize=QUEUE_MAX_SIZE)
        self._processed_queue: queue.Queue = queue.Queue(maxsize=QUEUE_MAX_SIZE)

    def start(self) -> None:
        """Start the pipeline and block until it stops."""
        self._is_running.set()

        threads = [
            threading.Thread(target=self._capture_loop, name="capture", daemon=True),
            threading.Thread(target=self._processing_loop, name="processing", daemon=True),
            threading.Thread(target=self._display_loop, name="display", daemon=True),
        ]

        for t in threads:
            t.start()

        for t in threads:
            t.join()

    def stop(self) -> None:
        """Signal all threads to stop."""
        self._is_running.clear()

    def _capture_loop(self) -> None:
        """Read frames from source and push into frame queue."""
        while self._is_running.is_set():
            ret, frame = self._source.read()
            if not ret:
                logger.info("Frame source exhausted")
                break
            try:
                self._frame_queue.put(frame, timeout=QUEUE_TIMEOUT_SEC)
            except queue.Full:
                continue

        self._is_running.clear()
        self._frame_queue.put(_SENTINEL)

    def _processing_loop(self) -> None:
        """Process frames from frame queue, push results to processed queue."""
        while self._is_running.is_set():
            try:
                frame = self._frame_queue.get(timeout=QUEUE_TIMEOUT_SEC)
            except queue.Empty:
                continue

            if frame is _SENTINEL:
                break

            result_frame, metadata = self._manager.process_frame(frame)
            concatenated = np.hstack((frame, result_frame))

            try:
                self._processed_queue.put(concatenated, timeout=QUEUE_TIMEOUT_SEC)
            except queue.Full:
                continue

        self._is_running.clear()
        self._processed_queue.put(_SENTINEL)

    def _display_loop(self) -> None:
        """Display processed frames from processed queue."""
        while self._is_running.is_set():
            try:
                processed_frame = self._processed_queue.get(timeout=QUEUE_TIMEOUT_SEC)
            except queue.Empty:
                continue

            if processed_frame is _SENTINEL:
                break

            cv2.imshow("Result", processed_frame)
            if cv2.waitKey(1) & 0xFF == 27:  # ESC key
                self.stop()
                break

        cv2.destroyAllWindows()
