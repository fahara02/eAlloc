
# Sphinx configuration file for eAlloc

import os
import sys

project = 'eAlloc'
copyright = '2024, Your Name/Company' # Update as needed
author = 'Your Name/Company' # Update as needed
release = '1.0.0' # Update as needed

extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.intersphinx',
    'sphinx.ext.todo',
    'sphinx.ext.mathjax',
    'sphinx.ext.ifconfig',
    'sphinx.ext.viewcode',
    'sphinx.ext.githubpages',
    'sphinx.ext.graphviz', # For Doxygen graphs if enabled
    'breathe',            # For C++ documentation from Doxygen
]

# Suppress specific warnings
suppress_warnings = ['cpp']  # Attempt to suppress all cpp domain warnings

# Theme configuration
try:
    import furo
    html_theme = 'furo'
    html_theme_options = {
        "light_css_variables": {
            "font-stack": "Arial, sans-serif",
            "font-stack--monospace": "Consolas, monospace",
        },
        "dark_css_variables": {
            "font-stack": "Arial, sans-serif",
            "font-stack--monospace": "Consolas, monospace",
        },
        # "sidebar_hide_name": True, # If you want to hide project name in sidebar
    }
except ImportError:
    html_theme = 'sphinx_rtd_theme'
    html_theme_options = {
        'navigation_depth': 4,
        'collapse_navigation': False,
    }

# Path to static files (e.g., custom CSS)
html_static_path = ['_static']
html_css_files = ['custom.css'] # Your custom CSS file

# Breathe configuration
breathe_projects = {
    "eAlloc": "../doxygen/xml"
}
breathe_default_project = "eAlloc"
breathe_default_members = ('members', 'undoc-members', 'protected-members', 'private-members')

# Tell Sphinx where to find C++ header files for linking (optional, Doxygen usually handles this)
breathe_projects_source = {
    "eAlloc": (os.path.abspath('../../src'), ['hpp', 'h'])
}

# Graphviz output format (SVG is good for web)
graphviz_output_format = 'svg'

# Other settings
templates_path = ['_templates']
source_suffix = '.rst'
master_doc = 'index' # For older Sphinx, root_doc for newer
language = 'en'
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']
pygments_style = 'sphinx' # For syntax highlighting
todo_include_todos = True
html_show_sourcelink = False # Set to True if you want "View page source" links
