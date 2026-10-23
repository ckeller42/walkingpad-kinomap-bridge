"""Sphinx configuration for the WalkingPad -> Kinomap bridge documentation.

The source directory is ``docs/`` itself, so the Markdown already in the repo
(and readable on GitHub) is the same text the rendered site serves: one copy,
no duplication. The C++ API is pulled in by Breathe from the Doxygen XML, so
the reference pages cannot drift from the headers.

Build (from the repo root)::

    doxygen Doxyfile                      # writes build/doxygen/xml
    sphinx-build -b html docs build/sphinx
"""

import os
import subprocess

project = "WalkingPad Kinomap Bridge"
author = "ckeller42"
copyright = "2026, ckeller42"

extensions = [
    "breathe",
    "myst_parser",
    "sphinx.ext.githubpages",
]

# --- Breathe -----------------------------------------------------------------
# Relative to this file, i.e. docs/../build/doxygen/xml.
_doxygen_xml = os.path.join(os.path.dirname(__file__), "..", "build", "doxygen", "xml")
breathe_projects = {"bridge": _doxygen_xml}
breathe_default_project = "bridge"
breathe_default_members = ("members",)
breathe_show_include = False

# On Read the Docs (and anywhere else without a prior Doxygen run) generate the
# XML as part of the Sphinx build, so the API pages are never silently empty.
if os.environ.get("READTHEDOCS") or not os.path.isdir(_doxygen_xml):
    _root = os.path.join(os.path.dirname(__file__), "..")
    try:
        os.makedirs(os.path.join(_root, "build", "doxygen"), exist_ok=True)
        subprocess.run(["doxygen", "Doxyfile"], cwd=_root, check=True)
    except (OSError, subprocess.CalledProcessError) as exc:  # pragma: no cover
        # Keep the narrative pages buildable without doxygen installed; the API
        # pages will warn, which is better than failing the whole build.
        print(f"conf.py: doxygen not run ({exc}); API pages will be empty")

# --- MyST --------------------------------------------------------------------
myst_enable_extensions = ["colon_fence", "deflist", "linkify"]
myst_heading_anchors = 3
suppress_warnings = ["myst.header"]

source_suffix = {".rst": "restructuredtext", ".md": "markdown"}
master_doc = "index"

exclude_patterns = [
    "superpowers/**",   # plans and specs: working material, not product docs
    "captures/**",      # raw evidence (logs, timelines), read by tools, not pages
    "_build",
    "Thumbs.db",
    ".DS_Store",
]

# --- HTML --------------------------------------------------------------------
html_theme = "furo"
html_title = project
html_static_path = []
