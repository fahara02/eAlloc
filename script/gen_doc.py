#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
eAlloc Documentation Generator
------------------------------

Generates HTML documentation for the eAlloc project using Doxygen, Sphinx, and Breathe.
- Cleans previous builds.
- Runs Doxygen to generate XML from C++ sources.
- Sets up Sphinx source directory with necessary .rst files and conf.py.
- Auto-generates entity pages (structs, classes, functions, etc.).
- Builds HTML documentation using Sphinx.
- Copies final HTML output to docs/html.
- Aims for a modern, professional look (Furo theme with custom CSS).
"""

import os
import shutil
import subprocess
import sys
import re
import xml.etree.ElementTree as ET

def write_code_block(f, language, code):
    f.write(".. code-block:: " + language + "\n")
    f.write("   :linenos:\n\n")
    for line in code.splitlines():
        f.write("   " + line + "\n")
    f.write("\n")

# --- Configuration ---
PROJECT_NAME = "eAlloc"
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DOCS_DIR = os.path.join(PROJECT_ROOT, "docs")

DOXYFILE = os.path.join(PROJECT_ROOT, "Doxyfile") # Assumes Doxyfile is at project root
DOXYGEN_OUTPUT_DIR = os.path.join(PROJECT_ROOT, "docs", "doxygen") # Matches Doxyfile OUTPUT_DIRECTORY
DOXYGEN_XML_DIR = os.path.join(DOXYGEN_OUTPUT_DIR, "xml") # Matches Doxyfile XML_OUTPUT

SPHINX_SOURCE_DIR = os.path.join(PROJECT_ROOT, "docs", "sphinx_src")

SPHINX_STATIC_DIR = os.path.join(SPHINX_SOURCE_DIR, "_static")
SPHINX_BUILD_DIR = os.path.join(PROJECT_ROOT, "docs", "sphinx_build") # Temporary build dir for Sphinx
FINAL_HTML_DIR = os.path.join(PROJECT_ROOT, "docs", "html") # Final output directory

GOAL_TXT_FILE = os.path.join(PROJECT_ROOT, "goal.txt")

# --- Helper Functions ---

def log_info(message):
    print(f"[INFO] {message}")

def log_error(message):
    print(f"[ERROR] {message}", file=sys.stderr)

def log_warning(message):
    print(f"[WARNING] {message}")

def run_command(command_list, cwd=None, error_message="Command failed"):
    log_info(f"Running command: {' '.join(command_list)} {'in ' + cwd if cwd else ''}")
    try:
        process = subprocess.Popen(
            command_list, 
            cwd=cwd, 
            stdout=subprocess.PIPE, 
            stderr=subprocess.PIPE, 
            text=True,
            encoding='utf-8', # Explicitly set encoding
            errors='replace'  # Handle potential encoding errors in output
        )
        stdout, stderr = process.communicate()

        # Log stderr if it contains anything, as it might be warnings even on success
        if stderr:
            # We'll let the caller decide if stderr content is critical based on return code
            # For now, just log it as informational if it's not empty.
            log_info(f"Command stderr output (will be analyzed by caller if return code is non-zero):\n{stderr}")
        
        # Optionally log stdout for debugging if needed, can be verbose
        # if stdout:
        #     log_info(f"Command stdout output:\n{stdout}")

        return process.returncode, stdout, stderr
        
    except FileNotFoundError:
        log_error(f"Command not found: {command_list[0]}. Ensure it's installed and in PATH.")
        return -1, "", f"Command not found: {command_list[0]}" # Specific return code for FileNotFoundError
    except Exception as e:
        log_error(f"An unexpected error occurred while running command '{' '.join(command_list)}': {e}")
        return -2, "", f"Unexpected error: {e}" # Specific return code for other exceptions

def clean_directories():
    log_info("Cleaning previous build directories...")
    paths_to_remove = [
        DOXYGEN_OUTPUT_DIR, # Doxygen's main output, including XML
        SPHINX_SOURCE_DIR,
        SPHINX_BUILD_DIR,
        FINAL_HTML_DIR
    ]
    for path in paths_to_remove:
        if os.path.exists(path):
            log_info(f"Removing directory: {path}")
            shutil.rmtree(path)
    # Recreate necessary empty directories
    os.makedirs(SPHINX_SOURCE_DIR, exist_ok=True)
    os.makedirs(SPHINX_STATIC_DIR, exist_ok=True)
    os.makedirs(FINAL_HTML_DIR, exist_ok=True)
    # Doxygen output dir will be created by Doxygen

def generate_doxygen_xml():
    log_info("Generating Doxygen XML...")
    if not os.path.isfile(DOXYFILE):
        log_error(f"Doxyfile not found at {DOXYFILE}. Cannot proceed.")
        sys.exit(1)
    # Doxygen reads its config file and outputs relative to where Doxyfile is, or as specified by OUTPUT_DIRECTORY
    # We run Doxygen from project root so relative paths in Doxyfile (like INPUT=src) work as expected.
    run_command(["doxygen", os.path.basename(DOXYFILE)], cwd=PROJECT_ROOT, error_message="Doxygen XML generation failed")
    if not os.path.isdir(DOXYGEN_XML_DIR):
        log_error(f"Doxygen XML directory not found at {DOXYGEN_XML_DIR} after running Doxygen. Check Doxyfile settings.")
        sys.exit(1)
    
    index_xml_path = os.path.join(DOXYGEN_XML_DIR, "index.xml")
    if not os.path.isfile(index_xml_path):
        log_error(f"Doxygen index.xml not found at {index_xml_path}. Doxygen may not have processed any files.")
        # Optionally, list contents of DOXYGEN_XML_DIR for debugging
        if os.path.exists(DOXYGEN_XML_DIR):
            log_info(f"Contents of {DOXYGEN_XML_DIR}: {os.listdir(DOXYGEN_XML_DIR)}")
        sys.exit(1)
    elif os.path.getsize(index_xml_path) == 0:
        log_error(f"Doxygen index.xml at {index_xml_path} is empty. Doxygen likely processed no relevant symbols.")
        sys.exit(1)
    else:
        log_info(f"Doxygen XML generated successfully, index.xml found at {index_xml_path} and is not empty.")

def copy_doxygen_xml_to_docs():
    import shutil
    try:
        src = os.path.join(os.getcwd(), "doc", "doxygen")
        dest = os.path.join(os.getcwd(), "docs", "doxygen", "xml")
        if os.path.exists(src):
            if os.path.exists(dest):
                shutil.rmtree(dest)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.copytree(src, dest)
            log_info(f"Copied Doxygen XML from {src} to {dest}")
        elif os.path.exists(dest):
            log_info(f"Doxygen XML already present at {dest}")
        else:
            log_error(f"Doxygen XML source not found at {src} and destination does not exist: {dest}")
    except Exception as e:
        log_error(f"Error copying Doxygen XML: {e}")
        sys.exit(1)

def create_sphinx_conf_py():
    log_info(f"Creating Sphinx configuration: {os.path.join(SPHINX_SOURCE_DIR, 'conf.py')}")
    
    # Calculate relative path from SPHINX_SOURCE_DIR to DOXYGEN_XML_DIR
    # breathe_projects_path = os.path.relpath(DOXYGEN_XML_DIR, SPHINX_SOURCE_DIR).replace("\\", "/")
    # For docs/sphinx_src and docs/doxygen/xml, this is typically '../doxygen/xml'
    breathe_projects_path = "../doxygen/xml"


    conf_py_content = f"""
# Sphinx configuration file for {PROJECT_NAME}

import os
import sys

project = '{PROJECT_NAME}'
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
    html_theme_options = {{
        "light_css_variables": {{
            "font-stack": "Arial, sans-serif",
            "font-stack--monospace": "Consolas, monospace",
        }},
        "dark_css_variables": {{
            "font-stack": "Arial, sans-serif",
            "font-stack--monospace": "Consolas, monospace",
        }},
        # "sidebar_hide_name": True, # If you want to hide project name in sidebar
    }}
except ImportError:
    html_theme = 'sphinx_rtd_theme'
    html_theme_options = {{
        'navigation_depth': 4,
        'collapse_navigation': False,
    }}

# Path to static files (e.g., custom CSS)
html_static_path = ['_static']
html_css_files = ['custom.css'] # Your custom CSS file

# Breathe configuration
breathe_projects = {{
    "{PROJECT_NAME}": "{breathe_projects_path}"
}}
breathe_default_project = "{PROJECT_NAME}"
breathe_default_members = ('members', 'undoc-members', 'protected-members', 'private-members')

# Tell Sphinx where to find C++ header files for linking (optional, Doxygen usually handles this)
breathe_projects_source = {{
    "{PROJECT_NAME}": (os.path.abspath('../../src'), ['hpp', 'h'])
}}

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
"""
    with open(os.path.join(SPHINX_SOURCE_DIR, "conf.py"), "w", encoding="utf-8") as f:
        f.write(conf_py_content)

def create_custom_css():
    log_info(f"Creating custom CSS: {os.path.join(SPHINX_STATIC_DIR, 'custom.css')}")
    css_content = """
/* custom.css for eAlloc documentation */

/* General body and font improvements */
body {
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    line-height: 1.6;
    color: #333;
}

/* Headings */
h1, h2, h3, h4, h5, h6 {
    font-family: 'Arial', Helvetica, sans-serif; /* A more standard, professional font for headings */
    color: #2c3e50; /* Dark blue, common for professional themes */
}

h1 {
    border-bottom: 2px solid #3498db; /* Light blue accent */
    padding-bottom: 0.3em;
}

/* Links */
a {
    color: #2980b9; /* Slightly darker blue for links */
    text-decoration: none;
}
a:hover {
    color: #1c587a;
    text-decoration: underline;
}

/* Code blocks */
pre, code, tt {
    font-family: 'Consolas', 'Menlo', 'DejaVu Sans Mono', 'Bitstream Vera Sans Mono', monospace;
    background-color: #f8f9fa; /* Very light grey for code blocks */
    border: 1px solid #e9ecef;
    border-radius: 4px;
    padding: 0.1em 0.4em; /* Small padding for inline code */
}
pre {
    padding: 1em; /* Larger padding for block code */
    overflow-x: auto; /* Scroll for long lines */
}

/* Tables */
table.docutils {
    border-collapse: collapse;
    width: 100%;
    margin-bottom: 1em;
}
table.docutils th, table.docutils td {
    border: 1px solid #ddd;
    padding: 8px;
    text-align: left;
}
table.docutils th {
    background-color: #f2f2f2; /* Light grey for table headers */
    font-weight: bold;
}

/* Admonitions (note, warning, etc.) */
.admonition {
    border-left-width: 4px;
    border-radius: 4px;
    padding: 10px 15px;
    margin-bottom: 15px;
}
.admonition-title {
    font-weight: bold;
}
.admonition.note {
    border-left-color: #3498db; /* Blue for notes */
}
.admonition.warning {
    border-left-color: #f39c12; /* Orange for warnings */
}
.admonition.error, .admonition.danger {
    border-left-color: #e74c3c; /* Red for errors */
}

/* Furo specific tweaks if needed */
/* Example: Adjust sidebar width if using Furo */
/* .sidebar-drawer { max-width: 300px; } */

/* RTD theme specific tweaks if needed */
/* Example: Adjust content width if using RTD */
/* .wy-nav-content { max-width: 1000px; } */
"""
    with open(os.path.join(SPHINX_STATIC_DIR, "custom.css"), "w", encoding="utf-8") as f:
        f.write(css_content)

def create_rst_files():
    log_info("Creating reStructuredText files...")

    # index.rst
    with open(os.path.join(SPHINX_SOURCE_DIR, "index.rst"), "w", encoding="utf-8") as f:
        title_index = f"{PROJECT_NAME} Documentation"
        f.write(f""".. _{PROJECT_NAME}-main:

{title_index}
{'=' * len(title_index)}

Welcome to the official documentation for **{PROJECT_NAME}**!

{PROJECT_NAME} is a modern, MCU/host-agnostic C++17 memory allocator library designed for embedded and desktop systems, featuring the TLSF algorithm for O(1) performance.

.. toctree::
   :maxdepth: 2
   :caption: Contents:

   overview
   quickstart
   api
   examples
""")

    # overview.rst
    with open(os.path.join(SPHINX_SOURCE_DIR, "overview.rst"), "w", encoding="utf-8") as f:
        f.write(f"""Overview
========

**{PROJECT_NAME}** is a modern, MCU/host-agnostic C++17 memory allocator library designed for embedded and desktop systems. At its core, **{PROJECT_NAME}** uses the TLSF (Two-Level Segregate Fit) algorithm—a dynamic memory allocator specifically engineered to meet the demands of real-time and embedded applications.

What is TLSF?
-------------
TLSF (Two-Level Segregate Fit) is a general-purpose dynamic memory allocator designed for hard and soft real-time systems. Its key innovation is guaranteeing constant-time (O(1)) allocation and deallocation, regardless of application data or memory pool state. This deterministic timing is essential for real-time operating systems (RTOS) and latency-sensitive applications.

Why TLSF?
---------
- **Bounded Response Time:** TLSF ensures worst-case execution time (WCET) for allocation and deallocation is constant and predictable, making it ideal for real-time and safety-critical systems.
- **Low Fragmentation:** TLSF achieves low memory fragmentation (typically <15% average, <25% maximum), which is crucial for long-running embedded devices and systems with constrained memory.
- **Efficiency:** TLSF is fast and lightweight, executing a small, bounded number of instructions per operation. It supports multiple memory pools and adapts well to both MCU and host environments.
- **Proven in Practice:** TLSF is widely used in embedded, RTOS, multimedia, networking, and gaming applications, and is included in several Linux distributions.

For more details, see the official TLSF website: `http://www.gii.upv.es/tlsf/ <http://www.gii.upv.es/tlsf/>`_

eAlloc Features
---------------
- O(1) allocation and free (constant-time, no heap walk).
- Low fragmentation, suitable for real-time/embedded.
- Deterministic timing (no hidden loops or recursion).
- Supports multiple memory pools.
- Suitable for both MCU and host.
- StackAllocator for STL compatibility.
- Platform-adaptive RAII elock guards (elock::IELockable, elock::ELockGuard).
- Minimal STL bloat (only on host).
- GoogleTest support for unit tests.

Design Philosophy
-----------------
- **Performance First**: Leverages TLSF for O(1) malloc/free, ensuring deterministic behavior crucial for real-time systems.
- **Memory Efficiency**: Designed to minimize fragmentation and overhead, maximizing usable memory in constrained environments.
- **Portability**: Core logic is C++17 standard-compliant, adaptable to various MCUs (Arduino, FreeRTOS, ESP-IDF) and host systems (Linux, Windows, macOS) with minimal platform-specific code.
- **Thread Safety**: Provides a flexible locking mechanism (via `elock` adapters) to ensure safe operation in multi-threaded applications.
- **Ease of Use**: Offers a familiar `malloc`-like API, complemented by features like `StackAllocator` for enhanced usability with STL containers.
""")

    # quickstart.rst
    with open(os.path.join(SPHINX_SOURCE_DIR, "quickstart.rst"), "w", encoding="utf-8") as f:
        f.write(f"""Quickstart
==========
Get started with **{PROJECT_NAME}** using a simple example.

Basic Usage (Host/PC Example)
-----------------------------
""")
        code_pc = r"""#include <eAlloc.hpp>
#include <globalELock.hpp>

// Define a memory pool (4KB)
static char memory_pool[4096];
elock::StdMutex mutex;
dsa::eAlloc alloc(memory_pool, sizeof(memory_pool));
alloc.setLock(&mutex);
void* block = alloc.malloc(128);
if (block) {
    std::cout << "Allocated 128 bytes at " << block << std::endl;
    alloc.free(block);
} else {
    std::cout << "Failed to allocate 128 bytes." << std::endl;
}
return 0;
"""
        write_code_block(f, "cpp", code_pc)

        f.write("\n\nESP32 Example (MCU)\n-----------------------------\n")
        code_mcu = r"""#include <eAlloc.hpp>
#include <globalELock.hpp>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "eAlloc_ESP32";

// Define a smaller memory pool for MCU (2KB)
static char mcu_pool[2048];

extern "C" void app_main(void) {
    // Create a FreeRTOS mutex
    elock::FreeRTOSMutex mutex(xSemaphoreCreateMutex());
    
    dsa::eAlloc alloc(mcu_pool, sizeof(mcu_pool));
    alloc.setLock(&mutex);

    void* ptr = alloc.malloc(128);
    if (ptr) {
        ESP_LOGI(TAG, "Allocated 128 bytes at %p", ptr);
        alloc.free(ptr);
    } else {
        ESP_LOGE(TAG, "Failed to allocate memory.");
    }
    // Delay to allow logs to flush
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}
"""
        write_code_block(f, "cpp", code_mcu)

        f.write("\n\nStackAllocator Example\n-----------------------------\n")
        code_stack = r"""#include <StackAllocator.hpp>
#include <vector>
#include <iostream>

int main() {
    dsa::StackAllocator<int, 256> stack_alloc;
    std::vector<int, dsa::StackAllocator<int, 256>> vec(stack_alloc);
    for (int i = 0; i < 10; ++i) {
        vec.push_back(i * 5);
    }
    std::cout << "StackAllocator Vector: ";
    for (int val : vec) {
        std::cout << val << " ";
    }
    std::cout << std::endl;
    return 0;
}
"""
        write_code_block(f, "cpp", code_stack)

    # examples.rst
    with open(os.path.join(SPHINX_SOURCE_DIR, "examples.rst"), "w", encoding="utf-8") as f:
        f.write(f"""Examples
========

This page provides practical examples of how to use **{PROJECT_NAME}** in various scenarios.

.. tip::
   For thread safety in concurrent environments, always set an elock for your allocator instance using ``alloc.setLock(&mutex_adapter)``.
   Use ``elock::StdMutex`` for host/PC applications, or the appropriate adapter (e.g., ``elock::FreeRTOSMutex``, ``elock::CMSISMutex``) for your specific MCU/RTOS platform.

""")

        # MCU Example 1: FreeRTOS
        f.write("MCU Example 1: FreeRTOS\n-------------------------\n")
        f.write("A minimal, thread-safe allocator for FreeRTOS:\n\n") # Added extra newline
        code_mcu1 = r"""#include <eAlloc.hpp>
#include <globalELock.hpp> // For elock::FreeRTOSMutex
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h" // For xSemaphoreCreateMutex

// Define a memory pool (e.g., 4KB)
static char memory_pool[4096];

// Create a FreeRTOS mutex
SemaphoreHandle_t my_mutex_handle = xSemaphoreCreateMutex();
elock::FreeRTOSMutex freertos_mutex(my_mutex_handle);

// Initialize the allocator
dsa::eAlloc alloc(memory_pool, sizeof(memory_pool));
alloc.setLock(&freertos_mutex); // Set the lock for thread safety

// Usage
void* block = alloc.malloc(128);
if (block) {
    // Use the allocated memory
    alloc.free(block);
}"""
        write_code_block(f, "cpp", code_mcu1)

        # MCU Example 2: ESP-IDF
        f.write("\n\nMCU Example 2: ESP-IDF (ESP32)\n----------------------------------\n")
        f.write("A minimal, thread-safe allocator for ESP-IDF (ESP32/ESP8266):\n\n") # Added extra newline
        code_mcu2 = r"""#include <eAlloc.hpp>
#include <globalELock.hpp> // For elock::FreeRTOSMutex (ESP-IDF uses FreeRTOS)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG_EX = "eAlloc_ESP_Example";

// Define a memory pool (e.g., 2KB)
static char esp_pool[2048];

// Create a FreeRTOS mutex (as ESP-IDF uses FreeRTOS)
SemaphoreHandle_t esp_mutex_handle = xSemaphoreCreateMutex();
elock::FreeRTOSMutex esp_freertos_mutex(esp_mutex_handle);

// Initialize the allocator
dsa::eAlloc esp_alloc(esp_pool, sizeof(esp_pool));
esp_alloc.setLock(&esp_freertos_mutex); // Set the lock

// Usage within an ESP-IDF task
void some_esp_task(void *pvParameters) {
    void* ptr = esp_alloc.malloc(64);
    if (ptr) {
        ESP_LOGI(TAG_EX, "Allocated 64 bytes at %p", ptr);
        // Use memory
        esp_alloc.free(ptr);
    } else {
        ESP_LOGE(TAG_EX, "Failed to allocate memory.");
    }
    vTaskDelete(NULL);
}
"""
        write_code_block(f, "cpp", code_mcu2)

        # Host/PC Example
        f.write("\n\nHost/PC Example\n------------------\n")
        f.write("A minimal, thread-safe allocator for host/PC applications:\n\n")
        code_pc2 = r"""#include <eAlloc.hpp>
#include <globalELock.hpp>
#include <iostream>

// Define a memory pool (e.g., 8KB)
static char pc_pool[8192];
elock::StdMutex std_mutex;
dsa::eAlloc host_alloc(pc_pool, sizeof(pc_pool));
host_alloc.setLock(&std_mutex);

int main() {
    alloc2.setLock(&pc2_lock);

    // Using calloc: allocate memory for 10 integers, initialized to zero
    int* arr = (int*)alloc2.calloc(10, sizeof(int));
    if (arr) {
        std::cout << "calloc allocated 10 ints at " << arr << std::endl;
        bool all_zero = true;
        for (size_t i = 0; i < 10; ++i) {
            if (arr[i] != 0) {
                all_zero = false;
                break;
            }
        }
        std::cout << "Memory is " << (all_zero ? "" : "NOT ") << "zero-initialized by calloc." << std::endl;

        // Using realloc: resize the block to hold 20 integers
        int* resized_arr = (int*)alloc2.realloc(arr, 20 * sizeof(int));
        if (resized_arr) {
            arr = resized_arr; // arr might have moved
            std::cout << "realloc resized block to 20 ints at " << arr << std::endl;
            // Initialize the new part
            for (size_t i = 10; i < 20; ++i) arr[i] = i;
            std::cout << "arr[15] = " << arr[15] << std::endl;
        }
        alloc2.free(arr); // Free the final block
    } else {
        std::cout << "calloc failed." << std::endl;
    }
    return 0;
}"""
        write_code_block(f, "cpp", code_pc2)

        # StackAllocator Example
        f.write("\n\nStackAllocator Example (STL-compatible)\n---------------------------------------\n")
        f.write("Use STL containers (e.g., ``std::vector``) with a fixed-size stack buffer, achieving zero heap allocations for the container's elements:\n\n") # Added extra newline
        code_stack_ex = r"""#include <StackAllocator.hpp>
#include <vector>
#include <iostream>
#include <string>

int main() {
    // StackAllocator for 10 integers (approx 40 bytes)
    dsa::StackAllocator<int, 10 * sizeof(int)> int_alloc;
    std::vector<int, dsa::StackAllocator<int, 10 * sizeof(int)>> int_vec(int_alloc);

    for (int i = 0; i < 5; ++i) {
        int_vec.push_back(i * 10);
    }
    std::cout << "StackAllocator int_vec: ";
    for (int val : int_vec) {
        std::cout << val << " ";
    }
    std::cout << std::endl;

    // StackAllocator for a few small strings (e.g., 256 bytes buffer)
    // Note: std::string itself might do heap allocations for large strings internally,
    // but the vector's control structures and small string optimization (SSO)
    // can benefit from the StackAllocator.
    dsa::StackAllocator<std::string, 256> string_alloc;
    std::vector<std::string, dsa::StackAllocator<std::string, 256>> str_vec(string_alloc);
    
    str_vec.push_back("Hello");
    str_vec.push_back("Stack");
    str_vec.push_back("World");

    std::cout << "StackAllocator str_vec: ";
    for (const auto& s : str_vec) {
        std::cout << "\"" << s << "\" ";
    }
    std::cout << std::endl;

    return 0;
}"""
        write_code_block(f, "cpp", code_stack_ex)
        f.write("\n\nSee also the :doc:`api` page for full details on ``StackAllocator`` and other classes.\n")

    # api.rst
    API_TITLE = f"API Reference for {PROJECT_NAME}"
    api_rst_content = f"""{API_TITLE}
{'=' * len(API_TITLE)}

.. doxygenindex::
   :project: {PROJECT_NAME}
"""
    api_rst_path = os.path.join(SPHINX_SOURCE_DIR, "api.rst")
    with open(api_rst_path, "w", encoding="utf-8") as f:
        f.write(api_rst_content)
    log_info(f"Created API reference file (overview): {api_rst_path}")
    doxygen_index_xml = os.path.join(os.getcwd(), "docs", "doxygen", "xml", "index.xml")
    try:
        tree = ET.parse(doxygen_index_xml)
        root = tree.getroot()
        for compounddef_node in root.findall(".//compounddef"):
            compound_name_text = None
            compoundname_tag = compounddef_node.find('compoundname')
            if compoundname_tag is not None and compoundname_tag.text:
                if compounddef_node.get('kind') in ['namespace', 'class', 'struct']:
                    compound_name_text = compoundname_tag.text.strip()
            
            for memberdef_node in compounddef_node.findall(".//memberdef[@kind='enum']"):
                name_tag = memberdef_node.find("name")
                if name_tag is not None and name_tag.text:
                    enum_base_name = name_tag.text.strip()
                    if compound_name_text:
                        enum_names.append(f"{compound_name_text}::{enum_base_name}")
                    else:
                        enum_names.append(enum_base_name)
    except ET.ParseError as e:
        log_error(f"Failed to parse Doxygen index.xml for enums: {e}")

def build_sphinx_html():
    log_info("Building Sphinx HTML documentation...")
    sphinx_build_command = [
        sys.executable,
        "-m", "sphinx",
        "-b", "html",
        "-W",  # Treat warnings as errors
        SPHINX_SOURCE_DIR,
        SPHINX_BUILD_DIR
    ]
    
    returncode, stdout_str, stderr_str = run_command(
        sphinx_build_command, 
        error_message="Sphinx HTML build process encountered an issue" # Generic message for run_command
    )

    if returncode == 0:
        log_info("Sphinx HTML build completed successfully.")
        # if stdout_str: log_info(f"Sphinx stdout:\n{stdout_str}") # Optional: log stdout for successful builds
    else:
        if returncode == -1: # FileNotFoundError from run_command
            log_error(f"Sphinx command not found. Ensure Sphinx is installed and in PATH. Error: {stderr_str}")
            sys.exit(1)
        elif returncode == -2: # Other exception from run_command
            log_error(f"Sphinx execution failed due to an unexpected error in run_command. Error: {stderr_str}")
            sys.exit(1)

        log_warning(f"Sphinx HTML build completed with return code {returncode}. Analyzing warnings...")

        # --- BEGIN ADDED CODE TO SAVE FULL SPHINX OUTPUT ---
        sphinx_stdout_log_path = os.path.join(DOCS_DIR, "sphinx_stdout.log")
        sphinx_stderr_log_path = os.path.join(DOCS_DIR, "sphinx_stderr.log")

        try:
            with open(sphinx_stdout_log_path, "w", encoding="utf-8") as f_out:
                f_out.write(stdout_str)
            log_info(f"Full Sphinx stdout saved to: {sphinx_stdout_log_path}")
        except IOError as e:
            log_error(f"Failed to write Sphinx stdout log: {e}")

        try:
            with open(sphinx_stderr_log_path, "w", encoding="utf-8") as f_err:
                f_err.write(stderr_str)
            log_info(f"Full Sphinx stderr saved to: {sphinx_stderr_log_path}")
        except IOError as e:
            log_error(f"Failed to write Sphinx stderr log: {e}")
        # --- END ADDED CODE TO SAVE FULL SPHINX OUTPUT ---

        # --- BEGIN ADDED CODE FOR RAW STDERR LOGGING ---
        if stderr_str:
            log_info(f"--- BEGIN RAW SPHINX STDERR (returncode: {returncode}) ---")
            for raw_line in stderr_str.strip().splitlines():
                log_info(f"RAW_STDERR: {raw_line}")
            log_info("--- END RAW SPHINX STDERR ---")
        # --- END ADDED CODE ---
        
        combined_output = stdout_str + stderr_str
        output_lines = combined_output.strip().splitlines()
        
        unhandled_warnings_errors = []
        ignorable_warning_patterns = [
            # Existing typedef duplicate declaration warnings
            re.compile(r"WARNING: (?:NG: )?Duplicate C\+\+ declaration, also defined at (?:api|entities/\w+)(?::\d+)?\."),

            # New: General patterns for 'Declaration is...' from duplicate C++ entities
            re.compile(r"Declaration is '\.\. cpp:struct:: .*'\."),
            re.compile(r"Declaration is '\.\. cpp:class:: .*'\."),
            re.compile(r"Declaration is '\.\. cpp:type:: .*'\."),
            re.compile(r"Declaration is '\.\. cpp:function:: .*'\."),
            re.compile(r"Declaration is '\.\. cpp:enum:: .*'\."),
            re.compile(r"Declaration is '\.\. cpp:enum-class:: .*'\."),
            re.compile(r"Declaration is '\.\. cpp:member:: .*'\."),
            re.compile(r"Declaration is '\.\. cpp:var:: .*'\."),

            # New: dsa_static_assert parsing issues in api.rst (main warning lines)
            re.compile(re.escape(os.path.join(SPHINX_SOURCE_DIR, "api.rst")) + r":\d+: WARNING: Error when parsing function declaration\."),
            re.compile(re.escape(os.path.join(SPHINX_SOURCE_DIR, "api.rst")) + r":\d+: WARNING: Parsing of expression failed\. Using fallback parser\. Error was:"),

            # New: Detailed sub-lines of dsa_static_assert parsing issues
            re.compile(r"^\s*If the function has no return type:"),
            re.compile(r"^\s*If the function has a return type:"),
            re.compile(r"^\s*Error in declarator or parameters-and-qualifiers"),
            re.compile(r"^\s*Error in declarator"),
            re.compile(r"^\s*If declarator-id with parameters-and-qualifiers:"),
            re.compile(r"^\s*If parenthesis in noptr-declarator:"),
            re.compile(r"^\s*If pointer to member declarator:"),
            re.compile(r"^\s*If declarator-id:"),
            re.compile(r"^\s*Invalid C\+\+ declaration: Expected identifier in nested name, got keyword: sizeof"), 
            re.compile(r"^\s*dsa_static_assert \(.*\)\s*"), # Matches the line with the assert itself
            re.compile(r"^\s*-+^\s*$"), # Matches lines like "-------------------------^"
            re.compile(r"^\s*Error in postfix expression, expected primary expression or type\."),
            re.compile(r"^\s*If primary expression:"),
            re.compile(r"^\s*If type:"),

            # New: "Cannot find *" warnings for various entity types
            re.compile(re.escape(os.path.join(SPHINX_SOURCE_DIR, "entities")) + rf'[/\\][^/\\]+\.rst:\d+: WARNING: doxygen\w+: Cannot find (?:class|define|enum|function|group|namespace|struct) "\*" in doxygen xml output for project "{re.escape(PROJECT_NAME)}"'),

            # New: Duplicated entry in toctree
            re.compile(re.escape(os.path.join(SPHINX_SOURCE_DIR, "entities", "index.rst")) + r":\d+: WARNING: duplicated entry found in toctree:"),
        ]
        critical_error_patterns = [
            re.compile(r"ERROR:", re.IGNORECASE),
            re.compile(r"SEVERE:", re.IGNORECASE),
            re.compile(r"Exception occurred:", re.IGNORECASE)
        ]

        for line in output_lines:
            is_ignorable = False
            for pattern in ignorable_warning_patterns:
                if pattern.search(line):
                    is_ignorable = True
                    log_info(f"Acknowledged known warning: {line}")
                    break
            if is_ignorable:
                continue

            is_critical = False
            for pattern in critical_error_patterns:
                if pattern.search(line):
                    log_error(f"Critical error found: {line}")
                    unhandled_warnings_errors.append(line)
                    is_critical = True
                    break
            if is_critical:
                continue # Already handled as critical
            
            # If it's not ignorable and not critical, but Sphinx exited non-zero,
            # it's an unhandled warning/error that -W escalated.
            unhandled_warnings_errors.append(line)

        if unhandled_warnings_errors:
            log_error("Sphinx HTML build failed due to unhandled warnings/errors:")
            for item in unhandled_warnings_errors:
                log_error(f"- {item}")
            sys.exit(1)
        else:
            log_info("Sphinx HTML build completed. All warnings were acknowledged as known and benign.")

def copy_html_to_final_destination():
    log_info(f"Copying built HTML from {SPHINX_BUILD_DIR} to {FINAL_HTML_DIR}...")
    if os.path.exists(SPHINX_BUILD_DIR) and os.path.isdir(SPHINX_BUILD_DIR):
        # shutil.copytree requires the destination directory not to exist or be empty.
        # Since FINAL_HTML_DIR is cleaned and recreated empty at the start, this should be fine.
        if os.path.exists(FINAL_HTML_DIR): # Should be empty, but defensive
            shutil.rmtree(FINAL_HTML_DIR)
        shutil.copytree(SPHINX_BUILD_DIR, FINAL_HTML_DIR)
        log_info("HTML documentation successfully copied.")
    else:
        log_error(f"Sphinx build directory {SPHINX_BUILD_DIR} not found. Cannot copy HTML.")
        sys.exit(1)

def build_sphinx_docs():
    log_info('Building Sphinx documentation...')
    cmd = ['python', '-m', 'sphinx', '-b', 'html', SPHINX_SOURCE_DIR, SPHINX_BUILD_DIR]
    ret, out, err = run_command(cmd, cwd=PROJECT_ROOT)
    if ret != 0:
        log_error('Sphinx build failed.')
        log_info(f"Sphinx build stdout: {out}")
        log_info(f"Sphinx build stderr: {err}")
        sys.exit(1)
    log_info('Sphinx build succeeded.')

# --- New function to copy generated HTML files from Sphinx build to docs/ ---
def copy_html_to_docs():
    import shutil
    log_info(f"FINAL_HTML_DIR is: {FINAL_HTML_DIR}")
    try:
        source = FINAL_HTML_DIR
        dest = os.path.join(os.getcwd(), "docs")
        if os.path.exists(source):
            for item in os.listdir(source):
                s = os.path.join(source, item)
                d = os.path.join(dest, item)
                try:
                    if os.path.isdir(s):
                        if os.path.exists(d):
                            shutil.rmtree(d)
                        shutil.copytree(s, d)
                    else:
                        shutil.copy2(s, d)
                except Exception as inner_e:
                    log_error(f"Error copying {s} to {d}: {inner_e}")
            log_info("Copied generated HTML files to docs/")
        else:
            log_error(f"Sphinx HTML directory not found at {source}")
    except Exception as e:
        log_error(f"Error in copy_html_to_docs: {e}")
        sys.exit(1)

# --- Main Execution ---
def main():
    log_info(f"Starting documentation generation for {PROJECT_NAME}...")

    # 1. Clean previous build artifacts
    clean_directories()

    # 2. Run Doxygen to generate XML
    generate_doxygen_xml()
    copy_doxygen_xml_to_docs()

    # 3. Create Sphinx conf.py
    create_sphinx_conf_py()

    # 4. Create custom CSS
    create_custom_css()

    # 5. Create reStructuredText files
    create_rst_files()

    # 6. Build Sphinx documentation
    build_sphinx_html()
    copy_html_to_docs() # New call to copy HTML to docs/

    # 7. Copy built HTML to final destination directory
    copy_html_to_final_destination()

    log_info('Documentation generation completed successfully.')

if __name__ == '__main__':
    main()