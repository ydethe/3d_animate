import logging

from rich.console import Console
from rich.logging import RichHandler

# Shared console so logging and progress bars render through the same stream.
console = Console(stderr=True)

logging.basicConfig(
    level=logging.INFO,
    format="%(message)s",
    datefmt="[%X]",
    handlers=[RichHandler(console=console, rich_tracebacks=True, show_path=False)],
)

logger = logging.getLogger(__name__)
