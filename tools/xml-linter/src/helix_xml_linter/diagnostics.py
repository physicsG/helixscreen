"""Diagnostic data types for helix-xml-linter lint results."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path


class Severity(Enum):
    """Diagnostic severity level."""

    ERROR = "error"
    WARNING = "warning"
    INFO = "info"


class CheckType(Enum):
    """Type of lint check that produced this diagnostic."""

    UNKNOWN_ATTRIBUTE = "unknown-attribute"
    UNKNOWN_WIDGET = "unknown-widget"
    INVALID_ENUM_VALUE = "invalid-enum-value"
    INVALID_BOOL_VALUE = "invalid-bool-value"
    UNKNOWN_STYLE_PROP = "unknown-style-prop"
    UNKNOWN_CONST_REF = "unknown-const-ref"
    UNKNOWN_STYLE_REF = "unknown-style-ref"
    UNKNOWN_SUBJECT_REF = "unknown-subject-ref"
    UNKNOWN_EVENT_REF = "unknown-event-ref"
    MISSING_REQUIRED = "missing-required"
    COMPONENT_PARAM_MISMATCH = "component-param-mismatch"
    XML_PARSE_ERROR = "xml-parse-error"
    UNKNOWN_FONT_REF = "unknown-font-ref"
    DEPRECATED_ATTRIBUTE = "deprecated-attribute"
    INVALID_STATE_QUALIFIER = "invalid-state-qualifier"
    NEGATED_CONST_REF = "negated-const-ref"


@dataclass
class Diagnostic:
    """A single lint diagnostic with location, severity, and context."""

    file: Path
    line: int
    column: int
    severity: Severity
    check: CheckType
    message: str
    element: str = ""
    attribute: str = ""
    value: str = ""

    def format_text(self) -> str:
        """Format as human-readable text: file:line:col: severity: message [check-type]."""
        parts = [
            f"{self.file}:{self.line}:{self.column}",
            f"{self.severity.value}:",
            self.message,
            f"[{self.check.value}]",
        ]
        return " ".join(parts)

    def format_json(self) -> dict[str, str | int]:
        """Format as a JSON-serializable dictionary."""
        result: dict[str, str | int] = {
            "file": str(self.file),
            "line": self.line,
            "column": self.column,
            "severity": self.severity.value,
            "check": self.check.value,
            "message": self.message,
        }
        if self.element:
            result["element"] = self.element
        if self.attribute:
            result["attribute"] = self.attribute
        if self.value:
            result["value"] = self.value
        return result

    def format_github(self) -> str:
        """Format as a GitHub Actions annotation: ::severity file=f,line=l,col=c::message."""
        # GitHub expects 'warning' not 'WARNING', etc.
        gh_severity = self.severity.value  # already lowercase from enum
        return (
            f"::{gh_severity} file={self.file},line={self.line},col={self.column}::{self.message}"
        )


@dataclass
class LintResult:
    """Aggregated lint result for one or more files."""

    file: Path
    diagnostics: list[Diagnostic] = field(default_factory=list)

    @property
    def is_clean(self) -> bool:
        """Return True if there are no error-severity diagnostics."""
        return not any(d.severity == Severity.ERROR for d in self.diagnostics)

    @property
    def has_warnings(self) -> bool:
        """Return True if there are warning-severity diagnostics."""
        return any(d.severity == Severity.WARNING for d in self.diagnostics)

    @property
    def error_count(self) -> int:
        """Return the number of error-severity diagnostics."""
        return sum(1 for d in self.diagnostics if d.severity == Severity.ERROR)

    @property
    def warning_count(self) -> int:
        """Return the number of warning-severity diagnostics."""
        return sum(1 for d in self.diagnostics if d.severity == Severity.WARNING)

    @property
    def info_count(self) -> int:
        """Return the number of info-severity diagnostics."""
        return sum(1 for d in self.diagnostics if d.severity == Severity.INFO)


def format_results_text(results: list[LintResult], quiet: bool = False) -> str:
    """Format multiple LintResults as human-readable text output."""
    lines: list[str] = []
    total_errors = 0
    total_warnings = 0
    total_files = 0

    for result in results:
        total_files += 1
        for diag in result.diagnostics:
            lines.append(diag.format_text())
            if diag.severity == Severity.ERROR:
                total_errors += 1
            elif diag.severity == Severity.WARNING:
                total_warnings += 1

    if not quiet:
        summary = (
            f"\n{total_files} file(s) checked, {total_errors} error(s), {total_warnings} warning(s)"
        )
        lines.append(summary)

    return "\n".join(lines)


def format_results_json(results: list[LintResult]) -> list[dict[str, str | int]]:
    """Format multiple LintResults as a flat list of JSON-serializable diagnostics."""
    output: list[dict[str, str | int]] = []
    for result in results:
        for diag in result.diagnostics:
            output.append(diag.format_json())
    return output


def format_results_github(results: list[LintResult]) -> str:
    """Format multiple LintResults as GitHub Actions annotations."""
    lines: list[str] = []
    for result in results:
        for diag in result.diagnostics:
            lines.append(diag.format_github())
    return "\n".join(lines)
