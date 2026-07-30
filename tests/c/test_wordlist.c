#include "cognator.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fputs(text, f) >= 0);
    assert(fclose(f) == 0);
}

static char *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    long size;
    char *buf;

    assert(f != NULL);
    assert(fseek(f, 0, SEEK_END) == 0);
    size = ftell(f);
    assert(size >= 0);
    assert(fseek(f, 0, SEEK_SET) == 0);
    buf = (char *)calloc((size_t)size + 1, 1);
    assert(buf != NULL);
    assert(fread(buf, 1, (size_t)size, f) == (size_t)size);
    assert(fclose(f) == 0);
    return buf;
}

int main(void) {
    cg_records *records = NULL;
    const cg_record *rec;
    char *text;

    write_text(
        "test_canonical.tsv",
        "ID\tLanguage_ID\tConcept\tSegments\tFamily\tCognacy\n"
        "w1\tlat\tNIGHT\tn o k t e\tromance\t42\n"
        "w2\tita\tNIGHT\tn o t t e\tromance\t42\n"
        "\n"
    );
    assert(cg_read_table("test_canonical.tsv", &records) == CG_OK);
    assert(cg_records_count(records) == 2);
    rec = cg_records_get(records, 0);
    assert(strcmp(rec->id, "w1") == 0);
    assert(strcmp(rec->language_id, "lat") == 0);
    assert(strcmp(rec->concept, "NIGHT") == 0);
    assert(strcmp(rec->segments, "n o k t e") == 0);
    assert(strcmp(rec->family, "romance") == 0);
    assert(strcmp(rec->cognacy, "42") == 0);
    cg_records_free(records);

    write_text(
        "test_alias.csv",
        "ID,Dataset,Language_ID,Glottocode,Parameter_ID,Concepticon_Gloss,Segments,Cognacy\n"
        "wp_haw_night,walworthpolynesian,walworthpolynesian_Hawaiian,hawa1245,p1,NIGHT,p o,wp_1;wp_2\n"
        "wp_ton_night,walworthpolynesian,walworthpolynesian_Tongan,tong1325,p1,NIGHT,p o,wp_1\n"
    );
    assert(cg_read_table("test_alias.csv", &records) == CG_OK);
    assert(cg_records_count(records) == 2);
    rec = cg_records_get(records, 0);
    assert(strcmp(rec->concept, "NIGHT") == 0);
    assert(strcmp(rec->family, "walworthpolynesian") == 0);
    assert(strcmp(rec->cognacy, "wp_1") == 0);
    cg_records_free(records);

    write_text(
        "test_native.csv",
        "av_id,Glottocode,concept_id,concept_label,Segments,Cognacy,source_form_id\n"
        "aav-alak,alak1253,kin-i,i,ʔ a l,,alak1253-1\n"
        "aav-bit,bit1240,kin-i,i,ʔ a l,,\n"
    );
    assert(cg_read_table("test_native.csv", &records) == CG_OK);
    assert(cg_records_count(records) == 2);
    assert(strcmp(cg_records_get(records, 0)->id, "aav-alak::alak1253-1") == 0);
    assert(strcmp(cg_records_get(records, 1)->id, "aav-bit::r3") == 0);
    cg_records_free(records);

    write_text(
        "test_skip.tsv",
        "ID\tLanguage_ID\tConcept\tSegments\n"
        "w1\tlat\tNIGHT\tn o k t e\n"
        "w2\tlat\tDAY\t\n"
    );
    assert(cg_read_table("test_skip.tsv", &records) == CG_OK);
    assert(cg_records_count(records) == 1);
    cg_records_free(records);

    write_text("test_missing.tsv", "ID\tLanguage_ID\tSegments\nw1\tlat\tp o\n");
    assert(cg_read_table("test_missing.tsv", &records) == CG_ERR_PARSE);
    assert(records == NULL);

    {
        cg_sidecar_row rows[1];
        memset(rows, 0, sizeof(rows));
        rows[0].id = "w1";
        rows[0].cognator_id = "romance:NIGHT:1";
        rows[0].confidence = 0.91;
        rows[0].assigned_support = 1.2;
        rows[0].alternative_support = 0.3;
        rows[0].support_margin = 0.9;
        rows[0].cluster_size = 2;
        rows[0].candidate_neighbors = 3;
        rows[0].bootstrap = 0.88;
        rows[0].bootstrap_low = 0.71;
        rows[0].bootstrap_high = 0.96;
        assert(cg_write_sidecar_rows("test_sidecar.tsv", rows, 1) == CG_OK);
        text = read_text("test_sidecar.tsv");
        assert(strstr(text, "ID\tCognator_ID\tCognator_Confidence") == text);
        assert(strstr(text, "\tromance:NIGHT:1\t0.910000\t1.200000\t0.300000\t0.900000\t2\t3\t0.880000\t0.710000\t0.960000\n") != NULL);
        free(text);
    }

    assert(cg_read_table(NULL, &records) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_read_table("test_canonical.tsv", NULL) == CG_ERR_INVALID_ARGUMENT);
    assert(cg_records_get(NULL, 0) == NULL);
    assert(cg_records_get(records, 99) == NULL);
    cg_records_free(NULL);
    return 0;
}
