#include "./ini_utils.h"
#include <string.h>
#include <stdlib.h>
#include "./ini.h"

#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, #n) == 0

#define POPULATE_CONFIG_KEY_VAL(section, key) \
	if (MATCH(section, key))              \
	{                                     \
		pconfig->key = strdup(value); \
		return 1;                     \
	}

#define POPULATE_CONFIG_KEY_VAL_RANGE(key, n) \
	POPULATE_CONFIG_KEY_VAL("SpecialSet", key##n)

static int read_ini_handler(void *config, const char *section, const char *name,
			    const char *value)
{
	configuration *pconfig = (configuration *)config;
	const char *section_name = "Basic_Threshold";

	POPULATE_CONFIG_KEY_VAL(section_name, SCapCbTest_OFF_Min)
	POPULATE_CONFIG_KEY_VAL(section_name, SCapCbTest_OFF_Max)
	POPULATE_CONFIG_KEY_VAL(section_name, SCapCbTest_ON_Min)
	POPULATE_CONFIG_KEY_VAL(section_name, SCapCbTest_ON_Max)
	ONE_THROUGH_16(POPULATE_CONFIG_KEY_VAL_RANGE, RawData_Max_High_Tx)
	ONE_THROUGH_16(POPULATE_CONFIG_KEY_VAL_RANGE, RawData_Min_High_Tx)

	return 0;
}

struct array_of_values parse_array_of_values(const char *array_string)
{
	int no_of_commas = 0;
	int slen = strlen(array_string);

	for (int i = 0; i < slen; i++)
	{
		if (array_string[i] == ',')
		{
			no_of_commas += 1;
		}
	}

	int *array = (int *)malloc(sizeof(int) * no_of_commas);

	int start = 0;
	for (int i = 0; i < no_of_commas; i++)
	{
		int j = 0;
		char numval[50];

		// printf("\nstart: %d", start);
		int next_char_idx = start;
		int real_char_offset = 0;

		while (array_string[next_char_idx] != ',')
		{
			if (array_string[next_char_idx] <= '9' && array_string[next_char_idx] >= '0')
			{
				numval[real_char_offset] = array_string[next_char_idx];
				real_char_offset++;
			}
			j++;
			next_char_idx = start + j;
			// printf("\nnext_char_idx: %d", next_char_idx);
		}

		numval[real_char_offset] = '\0';
		// printf("\n%s", numval);
		start = next_char_idx + 1;

		array[i] = atoi(numval);
	}

	struct array_of_values ret;
	ret.length = no_of_commas;
	ret.array = array;
	return ret;
}

#define COUNT_TX(key, counter, n) \
	ret = parse_array_of_values(config.key##n); \
	counter += ret.length; \
	free(ret.array);

#define APPEND_TX_VAL(key, arr, n) \
	ret = parse_array_of_values(config.key##n); \
	for (int i = 0; i < ret.length; i++) { \
		arr.array[arr.length] = ret.array[i]; \
		arr.length++; \
	} \
	free(ret.array);

int read_config(configuration *pconfig)
{
	if (ini_parse("test.ini", read_ini_handler, pconfig) < 0)
	{
		printf("Can't load 'test.ini'\n");
		return 1;
	}
	configuration config = *pconfig;

	int total_rawdata_max_high_tx = 0;
	int total_rawdata_min_high_tx = 0;

	struct array_of_values ret;
	ONE_THROUGH_16(COUNT_TX, RawData_Max_High_Tx, total_rawdata_max_high_tx)
	ONE_THROUGH_16(COUNT_TX, RawData_Min_High_Tx, total_rawdata_min_high_tx)

	int* rawdata_max_high_tx_all = (int*)malloc(sizeof(int) * total_rawdata_max_high_tx);
	int* rawdata_min_high_tx_all = (int*)malloc(sizeof(int) * total_rawdata_min_high_tx);

	struct array_of_values rawdata_max_high_tx;
	rawdata_max_high_tx.array = rawdata_max_high_tx_all;
	rawdata_max_high_tx.length = 0;
	ONE_THROUGH_16(APPEND_TX_VAL, RawData_Max_High_Tx, rawdata_max_high_tx)

	struct array_of_values rawdata_min_high_tx;
	rawdata_min_high_tx.array = rawdata_min_high_tx_all;
	rawdata_min_high_tx.length = 0;
	ONE_THROUGH_16(APPEND_TX_VAL, RawData_Min_High_Tx, rawdata_min_high_tx)

	pconfig->RawData_Max_High_Tx.array = rawdata_max_high_tx.array;
	pconfig->RawData_Max_High_Tx.length = rawdata_max_high_tx.length;

	pconfig->RawData_Min_High_Tx.array = rawdata_min_high_tx.array;
	pconfig->RawData_Min_High_Tx.length = rawdata_min_high_tx.length;
	return 0;
}

void free_array_of_values(struct array_of_values x) {
	free(x.array);
}
