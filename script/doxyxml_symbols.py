import os
import re
import xml.etree.ElementTree as ET

# ... (rest of your script setup and functions)

def get_doxygen_xml_symbols(xml_dir):
    """Parse Doxygen XML to get all classes, namespaces, and functions actually documented."""
    classes = set()
    namespaces = set()
    functions = set()
    # Parse index.xml for top-level symbols
    index_xml = os.path.join(xml_dir, "index.xml")
    if not os.path.exists(index_xml):
        return classes, namespaces, functions
    tree = ET.parse(index_xml)
    root = tree.getroot()
    for compound in root.findall("compound"):  # class, namespace, file, etc
        kind = compound.attrib.get("kind", "")
        name = compound.findtext("name")
        if kind == "class" or kind == "struct":
            classes.add(name)
        elif kind == "namespace":
            namespaces.add(name)
        elif kind == "function":
            functions.add(name)
        elif kind == "file":
            # Optionally, parse file for functions/classes
            pass
    # Additionally, parse all class/namespace/function XMLs for more symbols
    for fname in os.listdir(xml_dir):
        if fname.endswith(".xml") and fname != "index.xml":
            t = ET.parse(os.path.join(xml_dir, fname))
            r = t.getroot()
            for member in r.findall("sectiondef/memberdef"):
                kind = member.attrib.get("kind", "")
                name = member.findtext("name")
                if kind == "function":
                    functions.add(name)
    return classes, namespaces, functions

# ... (rest of your doc generation code)

def main():
    # ...
    xml_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "doxygen", "xml"))
    classes_xml, namespaces_xml, functions_xml = get_doxygen_xml_symbols(xml_dir)
    # Use only symbols present in XML for Sphinx/Breathe directives
    # ... (rest of your doc generation logic, replacing previous symbol emission)

if __name__ == "__main__":
    main()
