#ifndef KG_METADATA_H
#define KG_METADATA_H

/* Shared attribution and homepage for the C implementation and resources. */
#define KG_AUTHOR "nr084"
#define KG_HOMEPAGE_URL "https://github.com/nr084/KiTTY-ng"
/* RC compilers require one literal for each resource string. */
#define KG_AUTHOR_LABEL "Автор: nr084"
#define KG_HOMEPAGE_LABEL "Домашняя страница: https://github.com/nr084/KiTTY-ng"
#define KG_FILE_COMMENTS "Author: nr084. Homepage: https://github.com/nr084/KiTTY-ng"
#define KG_COPYRIGHT "Copyright (c) 2026 nr084. MIT License."
#define KG_WTEXT_INNER(value) L##value
#define KG_WTEXT(value) KG_WTEXT_INNER(value)

#endif
