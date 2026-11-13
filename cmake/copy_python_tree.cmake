# Makes DST a copy of the agent's files under SRC: the .py modules, packages
# included, and the .json lists they read. DST is emptied first, so a file
# removed from the source is not left behind.
file(REMOVE_RECURSE "${DST}")
file(COPY "${SRC}/" DESTINATION "${DST}"
    FILES_MATCHING PATTERN "*.py" PATTERN "*.json"
    PATTERN "__pycache__" EXCLUDE)
