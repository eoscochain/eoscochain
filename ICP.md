# Inter Chain Protocol (ICP)

## ICP Overview

建立于EOSIO软件堆栈之上的ICP跨链基础设施，可应用于EOSIO兼容的同构区块链的跨链通信。ICP设计之初，就考虑怎样以一种无侵入、可安全验证、去中心化的方式来实现EOS多链并行的同构跨链网络。经过对业界最前沿的跨链技术的研究（包括BTC Relay、Cosmos、Polkadot等），并结合对EOSIO软件实现细节的差异化剖析，ICP采取了比较现实的跨链方案：
- 实现类似于轻节点的跨链基础合约，对对端区块链进行区块头跟随和验证，使用Merkle树根和路径验证跨链交易的真实性，依赖全局一致的顺序来保证跨链交易同时遵循两条链的共识。
- 实现无需信任、去中心化的跨链中继，尽最大可能地向对端传递区块头和跨链交易，并对丢失、重复、超时、伪造的跨链消息进行合适的处理。

![ICP示意图](./images/icp-architecture.png)

## ICP Relay

ICP中继作为nodeos的插件，可随nodeos节点部署。部署模式上有几点需要说明：
- 不需要每个nodeos都开启ICP中继插件。
- 尽量多的nodeos开启ICP中继插件，将有助于保证跨链中继工作的不中断。
- 如果所有中继均瘫痪，将中断后续跨链交易进行，但不会影响已经发生的跨链交易；中继恢复后，将造成中断期某些跨链交易超时，但不会影响后续跨链交易的安全验证（这类似于所有nodeos节点瘫痪也会造成EOS区块链暂停）。
- 本端ICP中继可以连接对端多个ICP中继。
- 本端开启了ICP中继的nodeos之间可链内P2P互连(net_plugin/bnet_plugin)，但不可ICP P2P互连(icp_relay_plugin)。
- 本端ICP中继插件负责向本端跨链合约查询或发送交易，但不能直接向对端跨链合约查询或发送交易，而只能借助于与对端ICP中继的P2P通信。

![ICP多中继P2P网络](./images/icp-multiple-relays.png)

## ICP Network

基于EOSIO的两条同构区块链，需**对称**部署**一对**跨链中继和跨链合约。那么要达成多条区块链之间的ICP跨链通信，可在每两条链之间都这样部署。其实，从ICP基础设施的角度来说，ICP只负责**两条**区块链之间的跨链通信。如果要建立无感知的平滑跨越数条区块链的跨链通信网络，可在ICP基础合约之上编写合约构建**跨链网络协议(Inter Chain Network Protocol)**。

![ICP多链网络](./images/icp-network.png)

## ICP Components

#### ICP Relay Plugin

- [ICP Relay Plugin](https://github.com/eoscochain/eoscochain/tree/master/plugins/icp_relay_plugin): 
- [ICP Relay API Plugin](https://github.com/eoscochain/eoscochain/tree/master/plugins/icp_relay_api_plugin):

#### ICP Contract

- [ICP Contract](https://github.com/eoscochain/eoscochain/tree/master/contracts/icp)

#### ICP Token Contract

- [ICP Token Contract](https://github.com/eoscochain/eoscochain/tree/master/contracts/icp.token)
