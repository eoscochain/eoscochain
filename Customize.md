# Customization based on EOSIO

## Contracts

#### eosio.bios

新增：
- setcore: 原生币符号的设定不再依赖编译宏`CORE_SYMBOL`（重设需要编译），可在bios阶段调用setcore进行设置，将被写入状态数据库。

#### eosio.system

新增：
- setglobal: 允许启动阶段修改若干系统参数：
  - max_producer_schedule_size: 允许启动阶段修改区块生产者最大数量。
  - min_pervote_daily_pay
  - min_activated_stake
  - continuous_rate
  - to_producers_rate
  - to_bpay_rate
