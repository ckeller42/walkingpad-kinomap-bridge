# e2e_sim report

## boot: PASS

| step | result | evidence |
|---|---|---|
| `con stop -> *` | PASS | ok |
| `con status` | PASS | ok |
| `status src=sim belt_link=1 phone_link=0 armed=0 running=0` | PASS | status armed=0 running=0 belt_link=1 phone_link=0 belt_state=0 speed=0 target=0 wizard=off menu=off src=sim setup_done=1 rx_join=0 rx_lost=0 |

## refusals: PASS

| step | result | evidence |
|---|---|---|
| `con stop -> *` | PASS | ok |
| `con start -> err not-armed` | PASS | err not-armed |
| `udp absent /emit StartSequence/` | PASS | no match in 0.21 s (0 udp lines) |
| `con speed 3 -> err not-armed` | PASS | err not-armed |

## ui_start_stop: PASS

| step | result | evidence |
|---|---|---|
| `con stop -> *` | PASS | ok |
| `ftms connect` | PASS | connected at t=8.392 |
| `con arm` | PASS | ok |
| `con start` | PASS | ok |
| `udp /emit StartSequence tenths=20/ within 3 s` | PASS | t=9.481 emit StartSequence tenths=20 |
| `2acd >= 1.90 within 12 s` | PASS | t=16.416 2.00 km/h (00 00 c8 00) |
| `udp /emit NotifyStarted/ within 12 s` | PASS | t=9.481 emit NotifyStarted tenths=20 |
| `con status` | PASS | ok |
| `status running=1 belt_state=1 speed=20` | PASS | status armed=1 running=1 belt_link=1 phone_link=1 belt_state=1 speed=20 target=0 wizard=off menu=off src=sim setup_done=1 rx_join=0 rx_lost=0 |
| `con stop` | PASS | ok |
| `2acd == 0 within 8 s` | PASS | t=19.431 0.00 km/h (00 00 00 00) |
| `con status` | PASS | ok |
| `status armed=0 running=0 belt_state=4` | PASS | status armed=0 running=0 belt_link=1 phone_link=1 belt_state=4 speed=0 target=0 wizard=off menu=off src=sim setup_done=1 rx_join=0 rx_lost=0 |
| `ftms disconnect` | PASS | disconnected at t=19.613 |

## phone_start: PASS

| step | result | evidence |
|---|---|---|
| `con stop -> *` | PASS | ok |
| `ftms connect` | PASS | connected at t=20.797 |
| `con arm` | PASS | ok |
| `ftms cp 00` | PASS | written at t=21.067 |
| `indication 80 00 01 within 5 s` | PASS | t=21.097 80 00 01 |
| `ftms cp 07` | PASS | written at t=21.097 |
| `indication 80 07 01 within 5 s` | PASS | t=21.126 80 07 01 |
| `udp /emit StartSequence/ within 3 s` | PASS | t=21.566 emit StartSequence tenths=20 |
| `2acd >= 1.90 within 12 s` | PASS | t=28.521 2.00 km/h (00 00 c8 00) |
| `ftms cp 02 fa 00` | PASS | written at t=28.545 |
| `indication 80 02 01 within 5 s` | PASS | t=28.567 80 02 01 |
| `udp /emit SetSpeed tenths=25/ within 3 s` | PASS | t=29.552 emit SetSpeed tenths=25 |
| `2acd >= 2.40 within 8 s` | PASS | t=29.556 2.50 km/h (00 00 fa 00) |
| `ftms cp 08 01` | PASS | written at t=29.649 |
| `indication 80 08 01 within 5 s` | PASS | t=29.707 80 08 01 |
| `2acd == 0 within 8 s` | PASS | t=33.606 0.00 km/h (00 00 00 00) |
| `con status` | PASS | ok |
| `status running=0` | PASS | status armed=1 running=0 belt_link=1 phone_link=1 belt_state=4 speed=0 target=250 wizard=off menu=off src=sim setup_done=1 rx_join=0 rx_lost=0 |
| `ftms disconnect` | PASS | disconnected at t=33.738 |

## auto_mode: PASS

| step | result | evidence |
|---|---|---|
| `con stop -> *` | PASS | ok |
| `con arm` | PASS | ok |
| `con mode auto` | PASS | ok |
| `udp /emit SetMode mode=1/ within 3 s` | PASS | t=34.671 emit SetMode mode=1 |
| `udp /belt mode 3 -> 1 raw=0/ within 5 s` | PASS | t=36.725 belt mode 3 -> 1 raw=0 pending=0 active=1 |
| `con speed 3 -> err auto-mode` | PASS | err auto-mode |
| `udp absent /emit SetSpeed/` | PASS | no match in 0.12 s (0 udp lines) |
| `con mode manual` | PASS | ok |
| `udp /belt mode 1 -> 2 raw=1/ within 5 s` | PASS | t=38.768 belt mode 1 -> 2 raw=1 pending=0 active=0 |
| `udp /emit ResetPolicy/ within 5 s` | PASS | t=38.768 emit ResetPolicy tenths=0 |
| `con stop -> *` | PASS | ok |

## wizard: PASS

| step | result | evidence |
|---|---|---|
| `con stop -> *` | PASS | ok |
| `con wizard` | PASS | ok |
| `con status` | PASS | ok |
| `status wizard=siminfo` | PASS | status armed=0 running=0 belt_link=1 phone_link=0 belt_state=0 speed=0 target=250 wizard=siminfo menu=off src=sim setup_done=1 rx_join=0 rx_lost=0 |
| `con wizard next` | PASS | ok |
| `con wizard next` | PASS | ok |
| `con wizard next` | PASS | ok |
| `con status` | PASS | ok |
| `status wizard=off` | PASS | status armed=0 running=0 belt_link=1 phone_link=0 belt_state=4 speed=0 target=250 wizard=off menu=off src=sim setup_done=1 rx_join=0 rx_lost=0 |

## preconditions and bench

- PASS  precondition   port /dev/cu.usbmodem101 exists
- PASS  precondition   src=sim: status armed=0 running=0 belt_link=1 phone_link=0 belt_state=0 speed=0 target=0 wizard=off menu=off src=sim setup_done=1 rx_join=0 rx_lost=0
- PASS  precondition   udp alive: 192.168.178.96 t=4.368 udp log ON
- PASS  precondition   'WalkingPad Bridge' advertises: ftms connect + disconnect

0 unparsed udp line(s)
