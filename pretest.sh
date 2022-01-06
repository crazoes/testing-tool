echo 1 > /sys/kernel/debug/tracing/events/i2c/enable
echo adapter_nr==1 >/sys/kernel/debug/tracing/events/i2c/filter
echo > /sys/kernel/debug/tracing/trace
