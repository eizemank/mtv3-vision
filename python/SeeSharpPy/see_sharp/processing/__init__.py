"""Processing package.

Importing this package triggers processor registration via decorators.
To add a new processor, create a module and import it here.
"""

from see_sharp.processing.line_processor import LineProcessor
from see_sharp.processing.circle_processor import CircleProcessor
from see_sharp.processing.aruco_processor import ArucoProcessor
from see_sharp.processing.blob_processor import BlobProcessor
