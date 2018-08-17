# Customization based on EOSIO

## Genesis state

新增：
- core_symbol: 原生币符号的设定不再依赖编译宏`CORE_SYMBOL`（重设需要编译），由genesis状态中`core_symbol`属性来确定。

## Contracts

#### eosio.system

新增：
- setsched: 允许启动阶段修改区块生产者最大数量。
