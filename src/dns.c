#include "dns.h"

#include <stdio.h>

domain_t parse_domain(const char *buffer, size_t *offset) {
    char *labels[127];
    int labels_count = 0;

    domain_t domain;
    domain.len = 0;
    domain_t end_domain;
    end_domain.len = 0;

    while (1) {
        if (*(short *)(buffer + *offset) == 0) {
            break;
        }

        const char pointer_mask = (1 << 7) | (1 << 6);
        char is_pointer = (buffer[*offset] && pointer_mask) == pointer_mask;
        if (is_pointer) {
            short pointer_s = *(const short *)(buffer + *offset);
            pointer_s = pointer_s & ~((1 << 15) | (1 << 14));
            size_t pointer = ntohs(pointer_s);
            end_domain = parse_domain(buffer, &pointer);
            break;
        } else {
            char len = buffer[*offset];
            char *str = (char *)malloc(len);
            if (!str) {
                fprintf(stderr, "failed to allocate memory\n");
                exit(-1);
            }

            memcpy(str, buffer + *offset + 1, len);
            str[len] = 0;
            *offset += len + 1;

            labels[labels_count++] = str;
        }
    }

    domain.len = labels_count + end_domain.len;
    domain.labels = malloc(sizeof(char **) * domain.len);
    if (!labels) {
        fprintf(stderr, "failed to allocate memory\n");
        exit(-1);
    }

    for (int i = 0; i < labels_count; i++) {
        domain.labels[i] = labels[i];
    }

    for (int i = 0; i < end_domain.len; i++) {
        domain.labels[i + labels_count] = end_domain.labels[i];
    }

    if (end_domain.len) {
        free(end_domain.labels);
    }

    return domain;
}

char *domain_to_str(const domain_t *domain) {
    int total_len = 0;
    for (int i = 0; i < domain->len; i++) {
        total_len += strlen(domain->labels[i]) + 1;
    }
    total_len--;

    if (total_len <= 0) {
        printf("empty domain with len %i\n", total_len);
        return 0;
    }

    char *str = malloc(total_len + 1);
    if (!str) {
        fprintf(stderr, "failed to allocate memory\n");
        exit(-1);
    }

    for (int i = 0, offset = 0; i < domain->len; i++) {
        if (i != 0) {
            str[offset] = '.';
            offset++;
        }

        int len = strlen(domain->labels[i]);
        memcpy(str + offset, domain->labels[i], len);
        offset += len;
    }

    str[total_len] = 0;

    return str;
}

void free_domain(domain_t domain) {
    if (domain.labels == 0) {
        return;
    }

    for (int i = 0; i < domain.len; i++) {
        free(domain.labels[i]);
    }

    free(domain.labels);
}
