# Makes DST a copy of the .py files under SRC, packages included. DST is
# emptied first, so a module removed from the source is not left behind.
file(REMOVE_RECURSE "${DST}")
file(COPY "${SRC}/" DESTINATION "${DST}"
    FILES_MATCHING PATTERN "*.py"
    PATTERN "__pycache__" EXCLUDE)
