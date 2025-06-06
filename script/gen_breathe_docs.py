#!/usr/bin/env python3
"""
Breathe/Sphinx Documentation Generator for eAlloc

This script configures and runs Doxygen and Sphinx+Breathe to generate HTML API documentation
from your C++ sources with Doxygen-style comments.

- Requires: doxygen, sphinx, breathe, sphinx_rtd_theme
- Usage: python gen_breathe_docs.py

It will:
 1. Generate Doxygen XML in ./docs/doxygen/xml
 2. Generate Sphinx HTML docs in ./docs/html
"""
import os
import subprocess
import sys

PROJECT_NAME = "eAlloc"
# All paths relative to project root
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DOXYGEN_CONFIG = os.path.join(PROJECT_ROOT, "Doxyfile")
SPHINX_DIR = os.path.join(PROJECT_ROOT, "docs", "sphinx")
DOXYGEN_XML_DIR = os.path.join(PROJECT_ROOT, "docs", "doxygen", "xml")
HTML_DIR = os.path.join(PROJECT_ROOT, "docs")
SRC_DIR = os.path.join(PROJECT_ROOT, "src")

# 1. Write a minimal Doxyfile if not present
def ensure_doxyfile():
    if not os.path.isdir(SRC_DIR):
        print(f"[ERROR] Source directory '{SRC_DIR}' does not exist. Run from within the project.")
        sys.exit(1)
    if os.path.isfile(DOXYGEN_CONFIG):
        print(f"[WARN] Overwriting existing Doxyfile at {DOXYGEN_CONFIG}")
    with open(DOXYGEN_CONFIG, "w") as f:
        f.write(f"""
# Doxyfile for {PROJECT_NAME} (auto-generated)
PROJECT_NAME           = {PROJECT_NAME}
OUTPUT_DIRECTORY       = docs/doxygen
GENERATE_XML           = YES
GENERATE_HTML          = NO
INPUT                  = src
FILE_PATTERNS          = *.hpp *.h *.cpp
RECURSIVE              = YES

# Extraction and Parsing
EXTRACT_ALL            = YES
EXTRACT_PRIVATE        = YES
EXTRACT_STATIC         = YES
EXTRACT_LOCAL_CLASSES  = YES
EXTRACT_LOCAL_METHODS  = YES
EXTRACT_ANON_NSPACES   = YES
EXTRACT_PACKAGE        = YES
EXTRACT_STATIC         = YES
EXTRACT_NAMESPACE      = YES
EXTRACT_TYPEDEFS       = YES
EXTRACT_ENUMS          = YES
EXTRACT_MACROS         = YES
EXTRACT_STRUCTS        = YES
EXTRACT_UNIONS         = YES
EXTRACT_CLASSES        = YES

# Member and Grouping
HIDE_UNDOC_MEMBERS     = NO
HIDE_UNDOC_CLASSES     = NO
SHOW_GROUPED_MEMB_INC  = YES
SHOW_USED_FILES        = YES
SHOW_FILES             = YES
SHOW_NAMESPACES        = YES

# Source Browsing and Graphs
SOURCE_BROWSER         = YES
INLINE_SOURCES         = YES
REFERENCED_BY_RELATION = YES
REFERENCES_RELATION    = YES
CALL_GRAPH             = YES
CALLER_GRAPH           = YES
GRAPHICAL_HIERARCHY    = YES
DIRECTORY_GRAPH        = YES
DOT_MULTI_TARGETS      = YES
HAVE_DOT               = YES
CLASS_DIAGRAMS         = YES

# Preprocessor and Parsing
MACRO_EXPANSION        = YES
EXPAND_ONLY_PREDEF     = NO
SEARCH_INCLUDES        = YES
ENABLE_PREPROCESSING   = YES
SKIP_FUNCTION_MACROS   = NO

# C++ Parsing
EXTENSION_MAPPING      = h=Cpp
CLANG_ASSISTED_PARSING = YES
CLANG_OPTIONS          = --std=c++17

# Warnings and Diagnostics
WARNINGS               = YES
WARN_IF_UNDOCUMENTED   = YES
WARN_IF_DOC_ERROR      = YES
WARN_NO_PARAMDOC       = YES
QUIET                  = NO

# Output
GENERATE_TREEVIEW      = YES
GENERATE_DEPRECATEDLIST= YES

# Commented: enable HTML if you want standalone Doxygen HTML output
# GENERATE_HTML         = YES
""")
    print("[INFO] Wrote default Doxyfile with INPUT=src.")

# 2. Write minimal Sphinx conf.py and index.rst if not present
def ensure_sphinx():
    os.makedirs(SPHINX_DIR, exist_ok=True)
    conf_py = os.path.join(SPHINX_DIR, "conf.py")
    index_rst = os.path.join(SPHINX_DIR, "index.rst")
    if not os.path.isfile(conf_py):
        with open(conf_py, "w") as f:
            f.write(f"""
project = '{PROJECT_NAME}'
extensions = ['breathe']
html_theme = 'sphinx_rtd_theme'
breathe_projects = {{
    '{PROJECT_NAME}': '../doxygen/xml'
}}
breathe_default_project = '{PROJECT_NAME}'
master_doc = 'index'
""")
        print("[INFO] Wrote default Sphinx conf.py.")
    # Enhanced: auto-list all files, classes, functions, namespaces
    def list_source_files():
        files = []
        for fname in os.listdir(SRC_DIR):
            if fname.endswith(('.hpp', '.h', '.cpp')):
                files.append(fname)
        return sorted(files)
    def extract_classes_and_functions():
        import re
        classes = set()
        functions = set()
        namespaces = set()
        for fname in list_source_files():
            path = os.path.join(SRC_DIR, fname)
            with open(path, encoding='utf-8', errors='ignore') as f:
                content = f.read()
                for m in re.finditer(r'\b(class|struct)\s+([a-zA-Z_][a-zA-Z0-9_:]*)', content):
                    classes.add(m.group(2))
                for m in re.finditer(r'\bnamespace\s+([a-zA-Z_][a-zA-Z0-9_]*)', content):
                    namespaces.add(m.group(1))
                for m in re.finditer(r'^[a-zA-Z_][a-zA-Z0-9_:<>]*\s+([a-zA-Z_][a-zA-Z0-9_:]*)\s*\(', content, re.MULTILINE):
                    name = m.group(1)
                    if name not in classes and name not in namespaces:
                        functions.add(name)
        return sorted(classes), sorted(functions), sorted(namespaces)
    files = list_source_files()
    # Parse actual Doxygen XML output for available symbols
    xml_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "doxygen", "xml"))
    import sys
    sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
    from doxyxml_symbols import get_doxygen_xml_symbols
    classes_xml, namespaces_xml, functions_xml = get_doxygen_xml_symbols(xml_dir)
    with open(index_rst, "w", encoding='utf-8') as f:
        # Fixed, user-oriented homepage up to API Reference
        f.write(f"{PROJECT_NAME}\n{'='*len(PROJECT_NAME)}\n\n")
        f.write("Welcome to the eAlloc documentation!\n\n")
        f.write(".. contents::\n   :local:\n   :depth: 1\n\n")
        f.write("Overview\n========\neAlloc is a modern, MCU/host-agnostic C++17 memory allocator library designed for embedded and desktop systems. At its core, eAlloc uses the TLSF (Two-Level Segregate Fit) algorithm—a dynamic memory allocator specifically engineered to meet the demands of real-time and embedded applications.\n\n**What is TLSF?**\nTLSF (Two-Level Segregate Fit) is a general-purpose dynamic memory allocator designed for hard and soft real-time systems. Its key innovation is guaranteeing constant-time (O(1)) allocation and free operations, regardless of application data or memory pool state. This deterministic timing is essential for real-time operating systems (RTOS) and latency-sensitive applications.\n\n**Why TLSF?**\n- **Bounded Response Time:** TLSF ensures worst-case execution time (WCET) for allocation and deallocation is constant and predictable, making it ideal for real-time and safety-critical systems.\n- **Low Fragmentation:** TLSF achieves low memory fragmentation (typically <15% average, <25% maximum), which is crucial for long-running embedded devices and systems with constrained memory.\n- **Efficiency:** TLSF is fast and lightweight, executing a small, bounded number of instructions per operation. It supports multiple memory pools and adapts well to both MCU and host environments.\n- **Proven in Practice:** TLSF is widely used in embedded, RTOS, multimedia, networking, and gaming applications, and is included in several Linux distributions.\n\nFor more details, see the official TLSF website: http://www.gii.upv.es/tlsf/\n\n**eAlloc Features:**\n- O(1) allocation and free (constant-time, no heap walk)\n- Low fragmentation, suitable for real-time/embedded\n- Deterministic timing (no hidden loops or recursion)\n- Supports multiple memory pools\n- Suitable for both MCU and host\n\nOther Features:\n- StackAllocator for STL compatibility\n- Platform-adaptive RAII elock guards (elock::IELockable, elock::ELockGuard)\n- Minimal STL bloat (only on host)\n- GoogleTest support for unit tests\n\n")
        f.write("Quickstart\n==========\n\nGet started with eAlloc in just a few lines. The API is the same for embedded MCUs and PC/host builds—just use the right elock adapter for your platform.\n\nPlatform Support\n---------------\n- **MCU/RTOS**: FreeRTOS, CMSIS RTOS, Zephyr, ThreadX, Mbed, Arduino, ESP-IDF\n- **Host/PC**: Linux, Windows, macOS (uses std::timed_mutex)\n\n.. tip::\n   For thread safety, always set a elock via ``alloc.setELock(&mutex)``.\n   Use ``elock::StdMutex`` for host/PC, or the appropriate adapter for your platform.\n\n")
        mcu_title = "MCU/Embedded (FreeRTOS)"
        esp_title = "MCU/Embedded (ESP-IDF)"
        host_title = "PC/Host (std::timed_mutex)"
        f.write(f"{mcu_title}\n{'-'*len(mcu_title)}\nA minimal, thread-safe allocator for FreeRTOS:\n\n.. code-block:: cpp\n   :linenos:\n\n   #include <eAlloc.hpp>\n   #include <globalELock.hpp>\n   static char pool[4096];\n   elock::FreeRTOSMutex mutex(xSemaphoreCreateMutex());\n   dsa::eAlloc alloc(pool, sizeof(pool));\n   alloc.setELock(&mutex);\n   void* p = alloc.malloc(128);\n   alloc.free(p);\n\n")
        f.write(f"{esp_title}\n{'-'*len(esp_title)}\nA minimal, thread-safe allocator for ESP-IDF (ESP32/ESP8266):\n\n.. code-block:: cpp\n   :linenos:\n\n   #include <eAlloc.hpp>\n   #include <globalELock.hpp>\n   #include <freertos/FreeRTOS.h>\n   #include <freertos/semphr.h>\n   static char pool[4096];\n   SemaphoreHandle_t sem = xSemaphoreCreateMutex();\n   elock::FreeRTOSMutex mutex(sem);\n   dsa::eAlloc alloc(pool, sizeof(pool));\n   alloc.setELock(&mutex);\n   void* p = alloc.malloc(256);\n   alloc.free(p);\n\n")
        f.write(f"{host_title}\n{'-'*len(host_title)}\nA minimal, thread-safe allocator for desktop/host:\n\n.. code-block:: cpp\n   :linenos:\n\n   #include <eAlloc.hpp>\n   #include <globalELock.hpp>\n   static char pool[4096];\n   std::timed_mutex mtx;\n   elock::StdMutex mutex(mtx);\n   dsa::eAlloc alloc(pool, sizeof(pool));\n   alloc.setELock(&mutex);\n   void* p = alloc.malloc(128);\n   alloc.free(p);\n\n")
        # StackAllocator example
        stack_title = "StackAllocator (STL-compatible)"
        f.write(f"{stack_title}\n{'-'*len(stack_title)}\nUse STL containers (e.g. std::vector) with a fixed-size stack buffer and zero heap allocation:\n\n.. code-block:: cpp\n   :linenos:\n\n   #include <StackAllocator.hpp>\n   #include <vector>\n\n   dsa::StackAllocator<int, 128> alloc;\n   std::vector<int, dsa::StackAllocator<int, 128>> vec(alloc);\n   vec.push_back(42);\n\n")
        f.write("See also the StackAllocator class below for full API details.\n\n")
        # Dynamically inject API Reference from Doxygen XML
        f.write("API Reference\n=============" + "\n\n")
        f.write(".. rubric:: Main Classes\n\n")
        for ns in ("dsa",):
            if ns in namespaces_xml:
                f.write(f".. doxygennamespace:: {ns}\n   :project: eAlloc\n\n")
        for cls in ("eAlloc", "StackAllocator"):
            if cls in classes_xml:
                f.write(f".. doxygenclass:: {cls}\n   :project: eAlloc\n   :members:\n\n")
        f.write(".. rubric:: ELocking Interfaces\n\n")
        for ns in ("elock",):
            if ns in namespaces_xml:
                f.write(f".. doxygennamespace:: {ns}\n   :project: eAlloc\n\n")
        for cls in ("IELockable", "ELockGuard"):
            if cls in classes_xml:
                f.write(f".. doxygenclass:: {cls}\n   :project: eAlloc\n   :members:\n\n")
        f.write(".. rubric:: Functions\n\n")
        allowed_functions = [
            "eAlloc::malloc", "eAlloc::free", "eAlloc::realloc", "eAlloc::add_pool", "eAlloc::remove_pool", "eAlloc::check", "eAlloc::check_pool", "eAlloc::walk_pool", "eAlloc::integrity_walker", "eAlloc::logStorageReport", "eAlloc::report", "eAlloc::memalign", "eAlloc::get_pool"
        ]
        for func in allowed_functions:
            if func in functions_xml:
                f.write(f".. doxygenfunction:: {func}\n   :project: eAlloc\n\n")
        f.write(".. rubric:: Full API Index\n\n.. doxygenindex::\n   :project: eAlloc\n")
    print("[INFO] Wrote user-focused Sphinx index.rst with precise sections and deduplicated API reference.")

# 3. Run Doxygen to generate XML
def run_doxygen():
    print("[INFO] Running doxygen...")
    subprocess.check_call(["doxygen", DOXYGEN_CONFIG], cwd=PROJECT_ROOT)

# 4. Run Sphinx to generate HTML docs
def run_sphinx():
    print("[INFO] Running sphinx-build...")
    os.makedirs(HTML_DIR, exist_ok=True)
    try:
        subprocess.check_call([
            sys.executable, "-m", "sphinx", "-b", "html", SPHINX_DIR, HTML_DIR
        ], cwd=PROJECT_ROOT)
    except subprocess.CalledProcessError as e:
        if "No module named sphinx" in str(e):
            print("\n[ERROR] Sphinx is not installed. Please run: pip install sphinx sphinx_rtd_theme breathe\n")
        raise

if __name__ == "__main__":
    ensure_doxyfile()
    ensure_sphinx()
    run_doxygen()
    run_sphinx()
    print(f"[SUCCESS] Documentation generated in {HTML_DIR}")
