"""Blob detection processor with single-color and multi-color pattern matching.

Ported from C++ BlobProcessor (blob_processor.hpp / blob_processor.cpp).
This is the most complex processor, implementing graph-based multi-color
blob detection with weighted scoring.

Self-registers via @register_processor decorator (OCP).
"""

from __future__ import annotations

from itertools import product

import cv2
import numpy as np

from see_sharp.config import config_keys
from see_sharp.model.blob_detection.blob_params import (
    BlobParams,
    CriterionParams,
    LinkSettings,
    MultiColorBlobParams,
    NodeSettings,
    OneColorBlobParams,
    SizeMeasure,
)
from see_sharp.model.blob_detection.detected_blob import DetectedBlob
from see_sharp.model.blob_detection.detected_multicolor_blob import DetectedMulticolorBlob
from see_sharp.model.blob_detection.link_candidate import LinkCandidate
from see_sharp.model.blob_detection.node_candidate import NodeCandidate
from see_sharp.model.detected_object_meta import DetectedObjectMeta
from see_sharp.model.general_params import ProcessingType
from see_sharp.processing.frame_processor import FrameProcessor
from see_sharp.processing.processor_registry import register_processor

MORPHOLOGY_KERNEL_SIZE = (5, 5)


@register_processor(ProcessingType.BLOB_DETECTION)
class BlobProcessor(FrameProcessor):
    """Detects color blobs and multi-color patterns in video frames.

    Single-color detection: color range filtering in YCrCb space
    followed by contour analysis with morphological property filtering.

    Multi-color detection: graph-based pattern matching where nodes are
    single-color blobs and links define geometric relationships between them.
    A weighted scoring system determines pattern similarity.
    """

    def __init__(self, params: BlobParams) -> None:
        self._params = params

    @classmethod
    def from_config(cls, config: dict) -> BlobProcessor:
        """Create a BlobProcessor from the application config."""
        params = BlobParams.from_config(config[config_keys.BLOB_DETECTION])
        return cls(params)

    def process(self, frame: np.ndarray) -> tuple[np.ndarray, list[DetectedObjectMeta]]:
        """Detect blobs in the frame.

        Args:
            frame: Input BGR image.

        Returns:
            Tuple of (frame with drawn contours, list of blob metadata).
        """
        detected_one_color_blobs = self._get_one_color_blobs(frame)

        multicolor_blobs: list[DetectedMulticolorBlob] = []
        if self._params.enable_multicolor_detection and self._params.multicolor_blob_params:
            multicolor_blobs = self._get_multicolor_blobs(detected_one_color_blobs)

        metadata = self._get_metadata(detected_one_color_blobs)

        result_frame = frame.copy()
        self._draw_contours(result_frame, detected_one_color_blobs, multicolor_blobs)

        return result_frame, metadata

    # ---- Single-color blob detection ----

    def _get_one_color_blobs(self, frame: np.ndarray) -> dict[int, list[DetectedBlob]]:
        """Detect single-color blobs for each configured color pattern."""
        color_converted = cv2.cvtColor(frame, cv2.COLOR_BGR2YCrCb)

        detected: dict[int, list[DetectedBlob]] = {}
        for params in self._params.one_color_blob_params:
            mask = self._get_mask(params, color_converted)

            contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

            for contour in contours:
                blob = DetectedBlob(contour=contour, color_pattern_id=params.id)
                if not self._is_blob_fit(blob, params):
                    continue
                detected.setdefault(blob.color_pattern_id, []).append(blob)

        return detected

    def _get_mask(self, params: OneColorBlobParams, color_converted: np.ndarray) -> np.ndarray:
        """Create a binary mask from color range and apply morphology."""
        lower = np.array(params.lower_range, dtype=np.uint8)
        upper = np.array(params.upper_range, dtype=np.uint8)

        mask = cv2.inRange(color_converted, lower, upper)
        self._apply_morphology(mask)
        return mask

    @staticmethod
    def _apply_morphology(mask: np.ndarray) -> None:
        """Apply open+close morphology to clean up the mask in-place."""
        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, MORPHOLOGY_KERNEL_SIZE)
        cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel, dst=mask)
        cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel, dst=mask)

    @staticmethod
    def _is_blob_fit(blob: DetectedBlob, params: OneColorBlobParams) -> bool:
        """Check if a detected blob matches all single-color criteria."""
        x, y, w, h = blob.bounding_box
        return (
            params.min_area < blob.area < params.max_area
            and params.min_circularity < blob.circularity < params.max_circularity
            and params.min_inertia < blob.inertia < params.max_inertia
            and params.min_convexity < blob.convexity < params.max_convexity
            and w > params.min_width
            and h > params.min_height
        )

    # ---- Multi-color blob detection ----

    def _get_multicolor_blobs(
        self, detected_one_color_blobs: dict[int, list[DetectedBlob]]
    ) -> list[DetectedMulticolorBlob]:
        """Detect multi-color patterns by matching node/link graphs."""
        multicolor_blobs: list[DetectedMulticolorBlob] = []

        for mc_params in self._params.multicolor_blob_params:
            node_candidates = self._get_node_candidates(mc_params.nodes, detected_one_color_blobs)

            # Skip if any node has no candidates
            if any(len(group) == 0 for group in node_candidates):
                continue

            # Iterate all combinations of node candidates (replaces recursive iterateCombinations)
            for combination in product(*node_candidates):
                node_combination = list(combination)

                # Check node sizes
                if not self._get_and_check_node_sizes(
                    node_combination, mc_params.nodes, mc_params.size_measure
                ):
                    continue

                # Define and check link candidates
                base_node_size = node_combination[0].size
                base_node_angle = node_combination[0].detected_blob.max_axis.angle

                link_candidates = self._define_and_check_link_candidates(
                    node_combination, mc_params.links, base_node_size, base_node_angle
                )
                if link_candidates is None:
                    continue

                # Calculate node angles relative to base link
                base_link_angle = link_candidates[0].angle

                nodes_valid = True
                for i, node in enumerate(node_combination):
                    node_settings = mc_params.nodes[i]

                    # Compute relative angle
                    if node_settings.is_base_node():
                        node.angle = base_node_angle
                    else:
                        node.angle = node.detected_blob.max_axis.angle - base_link_angle

                    if not node_settings.angle.is_in_range(node.angle):
                        nodes_valid = False
                        break

                    # Calculate node similarity score
                    self._calculate_node_similarity(node, node_settings, mc_params.size_measure)

                    if node.score < node_settings.threshold:
                        nodes_valid = False
                        break

                if not nodes_valid:
                    continue

                # Calculate overall score
                overall_score = self._get_overall_score(
                    node_combination, link_candidates, mc_params.nodes, mc_params.links
                )

                if overall_score < mc_params.overall_threshold:
                    continue

                multicolor_blobs.append(
                    DetectedMulticolorBlob(
                        similarity=overall_score,
                        contour=self._get_merged_contours(node_combination),
                        node_candidates=node_combination,
                        link_candidates=link_candidates,
                    )
                )

        return multicolor_blobs

    def _get_node_candidates(
        self,
        nodes_settings: list[NodeSettings],
        detected_one_color_blobs: dict[int, list[DetectedBlob]],
    ) -> list[list[NodeCandidate]]:
        """Collect blob candidates that match each node's morphology requirements."""
        node_candidates: list[list[NodeCandidate]] = [[] for _ in nodes_settings]

        for i, node_settings in enumerate(nodes_settings):
            for color_id in node_settings.blob_color_pattern_ids:
                blobs = detected_one_color_blobs.get(color_id, [])
                for blob in blobs:
                    if self._is_blob_morphology_matching_node(blob, node_settings):
                        node_candidates[i].append(NodeCandidate(node_id=node_settings.id, detected_blob=blob))

        return node_candidates

    @staticmethod
    def _is_blob_morphology_matching_node(blob: DetectedBlob, node_settings: NodeSettings) -> bool:
        """Check if blob morphology matches node requirements."""
        return (
            node_settings.circularity.min <= blob.circularity <= node_settings.circularity.max
            and node_settings.inertia.min <= blob.inertia <= node_settings.inertia.max
            and node_settings.convexity.min <= blob.convexity <= node_settings.convexity.max
        )

    def _get_and_check_node_sizes(
        self,
        node_combination: list[NodeCandidate],
        nodes_settings: list[NodeSettings],
        size_measure: SizeMeasure,
    ) -> bool:
        """Compute and validate node sizes. Base node gets absolute size, others relative."""
        base_node_size = 0.0

        for i, (node, settings) in enumerate(zip(node_combination, nodes_settings)):
            node_size = self._get_relative_blob_size(
                base_node_size, node.detected_blob, settings, size_measure
            )

            if not settings.size.is_in_range(node_size):
                return False

            if settings.is_base_node():
                base_node_size = node_size

            node_combination[i].size = node_size

        return True

    def _define_and_check_link_candidates(
        self,
        node_combination: list[NodeCandidate],
        links_settings: list[LinkSettings],
        base_node_size: float,
        base_node_angle: float,
    ) -> list[LinkCandidate] | None:
        """Create link candidates, compute scores, and validate. Returns None if any fail."""
        link_candidates: list[LinkCandidate] = []
        base_link_length = 0.0
        base_link_angle = 0.0

        for link_settings in links_settings:
            first_idx = link_settings.get_first_node_id()
            second_idx = link_settings.get_second_node_id()

            p1 = node_combination[first_idx].detected_blob.center
            p2 = node_combination[second_idx].detected_blob.center

            if link_settings.is_base_link():
                link = LinkCandidate.create_base_link(
                    link_settings.id, p1, p2, base_node_size, base_node_angle
                )
                base_link_length = link.length
                base_link_angle = link.angle
            else:
                link = LinkCandidate.create_regular_link(
                    link_settings.id, p1, p2, base_link_length, base_link_angle
                )

            self._calculate_link_similarity(link_settings, link)

            if not self._is_link_matching(link, link_settings):
                return None

            link_candidates.append(link)

        return link_candidates

    @staticmethod
    def _calculate_node_similarity(
        node: NodeCandidate, settings: NodeSettings, size_measure: SizeMeasure
    ) -> None:
        """Calculate weighted similarity score for a node candidate."""
        blob = node.detected_blob
        size_score = settings.size.get_weighted_score(node.size)
        circ_score = settings.circularity.get_weighted_score(blob.circularity)
        inertia_score = settings.inertia.get_weighted_score(blob.inertia)
        convex_score = settings.convexity.get_weighted_score(blob.convexity)
        angle_score = settings.angle.get_weighted_score(node.angle)

        total_weight = (
            settings.size.weight
            + settings.circularity.weight
            + settings.inertia.weight
            + settings.convexity.weight
            + settings.angle.weight
            + 1e-5
        )
        node.score = (size_score + circ_score + inertia_score + convex_score + angle_score) / total_weight

    @staticmethod
    def _calculate_link_similarity(settings: LinkSettings, link: LinkCandidate) -> None:
        """Calculate weighted similarity score for a link candidate."""
        length_score = settings.length_absolute.get_weighted_score(link.length)
        rel_length_score = settings.length_relative.get_weighted_score(link.relative_length)
        angle_score = settings.angle_absolute.get_weighted_score(link.angle)
        rel_angle_score = settings.angle_relative.get_weighted_score(link.relative_angle)

        total_weight = (
            settings.length_absolute.weight
            + settings.length_relative.weight
            + settings.angle_absolute.weight
            + settings.angle_relative.weight
            + 1e-5
        )
        link.score = (length_score + rel_length_score + angle_score + rel_angle_score) / total_weight

    @staticmethod
    def _is_link_matching(link: LinkCandidate, settings: LinkSettings) -> bool:
        """Check if all link properties are within specified ranges."""
        return (
            settings.length_absolute.is_in_range(link.length)
            and settings.length_relative.is_in_range(link.relative_length)
            and settings.angle_absolute.is_in_range(link.angle)
            and settings.angle_relative.is_in_range(link.relative_angle)
            and link.score >= settings.threshold
        )

    @staticmethod
    def _get_overall_score(
        node_combination: list[NodeCandidate],
        link_candidates: list[LinkCandidate],
        nodes_settings: list[NodeSettings],
        links_settings: list[LinkSettings],
    ) -> float:
        """Calculate weighted average of all node and link scores."""
        total_score = 0.0
        total_weight = 0.0

        for node, settings in zip(node_combination, nodes_settings):
            total_score += settings.get_weighted_score(node.score)
            total_weight += settings.weight

        for link, settings in zip(link_candidates, links_settings):
            total_score += settings.get_weighted_score(link.score)
            total_weight += settings.weight

        return total_score / (total_weight + 1e-5)

    def _get_relative_blob_size(
        self,
        base_node_size: float,
        blob: DetectedBlob,
        node_settings: NodeSettings,
        size_measure: SizeMeasure,
    ) -> float:
        """Get blob size: absolute for base node, relative for others."""
        blob_size = self._get_blob_size(blob, size_measure)
        if node_settings.is_base_node():
            return blob_size
        return blob_size / base_node_size if base_node_size != 0 else 0.0

    @staticmethod
    def _get_blob_size(blob: DetectedBlob, size_measure: SizeMeasure) -> float:
        """Get the size of a blob using the specified measure."""
        if size_measure == SizeMeasure.MAX_AXIS:
            return blob.max_axis.length
        elif size_measure == SizeMeasure.WIDTH:
            return float(blob.bounding_box[2])
        elif size_measure == SizeMeasure.HEIGHT:
            return float(blob.bounding_box[3])
        elif size_measure == SizeMeasure.AREA:
            return blob.area
        elif size_measure == SizeMeasure.CONVEX_AREA:
            return float(blob.bounding_box[2] * blob.bounding_box[3])
        else:
            raise ValueError(f"Unknown SizeMeasure: {size_measure}")

    @staticmethod
    def _get_merged_contours(node_combination: list[NodeCandidate]) -> np.ndarray:
        """Merge all node contours into a single convex hull."""
        all_points = []
        for node in node_combination:
            if node.detected_blob is not None and len(node.detected_blob.contour) > 0:
                all_points.append(node.detected_blob.contour)

        if not all_points:
            return np.array([])

        merged = np.vstack(all_points)
        return cv2.convexHull(merged)

    # ---- Drawing and metadata ----

    @staticmethod
    def _draw_contours(
        result_frame: np.ndarray,
        blobs_by_color: dict[int, list[DetectedBlob]],
        multicolor_blobs: list[DetectedMulticolorBlob],
    ) -> None:
        """Draw detected contours on the result frame."""
        all_contours = []
        for blobs in blobs_by_color.values():
            for blob in blobs:
                all_contours.append(blob.contour)

        multicolor_contours = []
        for mc_blob in multicolor_blobs:
            if mc_blob.contour is not None and len(mc_blob.contour) > 0:
                multicolor_contours.append(mc_blob.contour)

        if all_contours:
            cv2.drawContours(result_frame, all_contours, -1, (255, 255, 255), 1)
        if multicolor_contours:
            cv2.drawContours(result_frame, multicolor_contours, -1, (0, 0, 255), 2)

    @staticmethod
    def _get_metadata(blobs_by_color: dict[int, list[DetectedBlob]]) -> list[DetectedObjectMeta]:
        """Convert detected blobs to generic metadata."""
        metadata: list[DetectedObjectMeta] = []
        for blobs in blobs_by_color.values():
            for blob in blobs:
                x, y, w, h = blob.bounding_box
                metadata.append(
                    DetectedObjectMeta(
                        id=blob.color_pattern_id,
                        center_x=blob.center[0],
                        center_y=blob.center[1],
                        area=blob.area,
                        bbox_x=x,
                        bbox_y=y,
                        bbox_w=w,
                        bbox_h=h,
                    )
                )
        return metadata
