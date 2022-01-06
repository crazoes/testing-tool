#define ONE_THROUGH_16(some_macro, args...) \
	some_macro(args, 1) \
	some_macro(args, 2) \
	some_macro(args, 3) \
	some_macro(args, 4) \
	some_macro(args, 5) \
	some_macro(args, 6) \
	some_macro(args, 7) \
	some_macro(args, 8) \
	some_macro(args, 9) \
	some_macro(args, 10) \
	some_macro(args, 11) \
	some_macro(args, 12) \
	some_macro(args, 13) \
	some_macro(args, 14) \
	some_macro(args, 15) \
	some_macro(args, 16)

#define struct_key_val_n(key, n) const char* key##n;

struct array_of_values {
	int* array;
	int length;
};

struct array_of_values parse_array_of_values(const char* array_string);

void free_array_of_values(struct array_of_values x);

typedef struct
{
	const char* SCapCbTest_OFF_Min;
	const char* SCapCbTest_OFF_Max;
	const char* SCapCbTest_ON_Min;
	const char* SCapCbTest_ON_Max;
	ONE_THROUGH_16(struct_key_val_n, RawData_Max_High_Tx)
	ONE_THROUGH_16(struct_key_val_n, RawData_Min_High_Tx)
	ONE_THROUGH_16(struct_key_val_n, Panel_Differ_Max_Tx)
	ONE_THROUGH_16(struct_key_val_n, Panel_Differ_Min_Tx)

	struct array_of_values RawData_Min_High_Tx;
	struct array_of_values RawData_Max_High_Tx;
} configuration;

#define LOOP_OVER_X(key, n) \
	ret = parse_array_of_values(config.key##n); \
	for (int i = 0; i < ret.length; i++) { \
			printf("\n%s%s %d %d", #key, #n, i, ret.array[i]); \
	} \
	free(ret.array);

int read_config(configuration* config);
