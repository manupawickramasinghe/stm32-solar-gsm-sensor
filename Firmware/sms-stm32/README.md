Notes on setting STM32 up

SIM_RECV_URC: +CFUN: 1
SIM_RECV_URC: 
SIM_RECV_URC: +CPIN: READY
SIM_RECV_URC: 
SIM_RECV_URC: RDY
SIM_RECV_URC: 
SIM_RECV_URC: +CFUN: 1
SIM_RECV_URC: 
SIM_RECV_URC: +CPIN: READY
SIM_RECV_URC: 
SIM_RECV_URC: RDY
SIM_RECV_URC: 
SIM_RECV_URC: +CFUN: 1
SIM_RECV_URC: 
SIM_RECV_URC: +CPIN: READY
SIM_RECV_URC: 
SIM_RECV_URC: RDY
SIM_RECV_URC: 
SIM_RECV_URC: +CFUN: 1
SIM_RECV_URC: 
SIM_RECV_URC: +CPIN: READY
SIM_RECV_URC: 
SIM_RECV_URC: RDY
SIM_RECV_URC: 
SIM_RECV_URC: +CFUN: 1
SIM_RECV_URC: 
SIM_RECV_URC: +CPIN: READY
SIM_RECV_URC: 
SIM_RECV_URC: +CFUN: 1
--- Reading DS18B20 Sensor ---
SMS Counter: 12
--- Sending SMS with Average Sensor Data ---
Attempting to get GSM timestamp...
AT+CCLK?

+CCLK: "25/08/02,21:10:59+22"

OK
Setting phone number: +94719593248
AT+CMGS="+94719593248"

> 
+CPIN: READY
Sending full message: 2025-08-02 21:10:59 - ID:00000 60min Avg - DHT H:0.0% T:0.0C; DS18B20 T:0.0C
2025-08-02 21:10:59 - ID:00000 60min Avg - DHT H:0.0% T:0.0C; DSMS send attempt finished.
Attempting to get GSM timestamp...
AT+CCLK?

+CCLK: "25/08/02,21:11:09+22"

OK

+CPIN: READY
Setting phone number: +94719751003
AT+CMGS="+94719751003"

> Sending full message: 2025-08-02 21:11:09 - ID:00000 60min Avg - DHT H:0.0% T:0.0C; DS18B20 T:0.0C
2025-08-02 21:11:09 - ID:00000 60min Avg - DHT H:0.0% T:0.0C; DSMS send attempt finished.
Attempting to get GSM timestamp...

+CPIN: READY
AT+CCLK?

+CCLK: "25/08/02,21:11:18+22"

OK
Setting phone number: +94768378406
AT+CMGS="+94768378406"

> Sending full message: 2025-08-02 21:11:18 - ID:00000 60min Avg - DHT H:0.0% T:0.0C; DS18B20 T:0.0C
2025-08-02 21:11:18 - ID:00000 60min Avg - DHT H:0.0% T:0.0C; DSMS send attempt finished.
------------------------------------
Reading cycle complete. Waiting for next cycle...
SIM_RECV_URC: 