#!/usr/bin/env python3

import argparse
import json


import subprocess

import time

args = None
logFile = None

unlockTimeout = 999999

systemAccounts = [
    'eosio.bpay',
    'eosio.msig',
    'eosio.names',
    'eosio.ram',
    'eosio.ramfee',
    'eosio.saving',
    'eosio.stake',
    'eosio.token',
    'eosio.vpay',
]


def jsonArg(a):
    return " '" + json.dumps(a) + "' "

def run(args):
    print('testtool.py:', args)
    logFile.write(args + '\n')
    if subprocess.call(args, shell=True):
        print('testtool.py: exiting because of error')
        #sys.exit(1)

def retry(args):
    while True:
        print('testtool.py:', args)
        logFile.write(args + '\n')
        if subprocess.call(args, shell=True):
            print('*** Retry')
        else:
            break

def background(args):
    print('testtool.py:', args)
    logFile.write(args + '\n')
    return subprocess.Popen(args, shell=True)

def getOutput(args):
    print('testtool.py:', args)
    logFile.write(args + '\n')
    proc = subprocess.Popen(args, shell=True, stdout=subprocess.PIPE)
    return proc.communicate()[0].decode('utf-8')

def getJsonOutput(args):
    print('testtool.py:', args)
    logFile.write(args + '\n')
    proc = subprocess.Popen(args, shell=True, stdout=subprocess.PIPE)
    return json.loads(proc.communicate()[0].decode('utf-8'))

def sleep(t):
    print('sleep', t, '...')
    time.sleep(t)
    print('resume')

# def stepCreateAccount():
#     print ("===========================    create contract account   ===========================" )
#     run(args.cleos + ' system newaccount producer111f %s  %s %s --stake-net "2.0000 SYS" --stake-cpu "2.0000 SYS" --buy-ram-kbytes 300 ' %(args.contract,args.public_key,args.public_key ))
#     run(args.cleos + ' system newaccount producer111g %s  %s %s --stake-net "2.0000 SYS" --stake-cpu "2.0000 SYS" --buy-ram-kbytes 300 ' %(args.contract2,args.public_key,args.public_key ))
#     run(args.cleos + ' get account %s' % args.contract)
#     run(args.cleos + ' get account %s' % args.contract2)

def stepInitCaee():
    print ("===========================    set contract caee   ===========================" )
    run(args.cleos + 'set contract %s ../randomdemo' %args.contract )
    run(args.cleos + 'set contract %s ../randomdemo' %args.contract2 )
    run(args.cleos + 'set account permission %s active \'{"threshold": 1,"keys": [{"key": "EOS8Znrtgwt8TfpmbVpTKvA2oB8Nqey625CLN8bCN3TEbgx86Dsvr","weight": 1}],"accounts": [{"permission":{"actor":"%s","permission":"eosio.code"},"weight":1}]}\' ' % (args.contract,args.contract))
    print ("sleep 5")


def stepClear():
    print ("===========================    set contract clear   ===========================" )
    run(args.cleos + 'push action %s clear "[]" -p %s ' %(args.contract, args.contract))
    run(args.cleos + 'get table %s %s seedobjs'  %(args.contract, args.contract) )
    run(args.cleos + 'push action %s clear "[]" -p %s ' %(args.contract2, args.contract2))
    run(args.cleos + 'get table %s %s seedobjs'  %(args.contract2, args.contract2) )
    print ("sleep 5")


def stepGenerate():
    print ("===========================    set contract stepGenerate   ===========================" )
    # run(args.cleos + 'push action %s generate \'[{"loop":1, "num":1}]\' -p %s -f' %(args.contract, args.contract))
    run(args.cleos + 'push action %s inlineact \'[{"payer":"%s", "in":"%s"}]\' -p %s  -f' %(args.contract,args.contract,args.contract2, args.contract))
    run(args.cleos + 'get table %s %s seedobjs'  %(args.contract, args.contract) )
    run(args.cleos + 'get table %s %s seedobjs'  %(args.contract2, args.contract2) )
    print ("sleep 5")


parser = argparse.ArgumentParser()

commands = [
    # ('o', 'create',      stepCreateAccount,             True,         "stepInitCaee"),
    ('i', 'init',      stepInitCaee,                    True,         "stepInitCaee"),
    ('c', 'clear',      stepClear,                      True,         "stepInitCaee"),
    ('g', 'generate',      stepGenerate,                True,         "stepInitCaee"),
]

parser.add_argument('--public-key', metavar='', help="EOSIO Public Key", default='EOS8Znrtgwt8TfpmbVpTKvA2oB8Nqey625CLN8bCN3TEbgx86Dsvr', dest="public_key")
parser.add_argument('--private-Key', metavar='', help="EOSIO Private Key", default='5K463ynhZoCDDa4RDcr63cUwWLTnKqmdcoTKTHBjqoKfv4u5V7p', dest="private_key")
parser.add_argument('--cleos', metavar='', help="Cleos command", default='../../build/programs/cleos/cleos --wallet-url http://127.0.0.1:6666 --url http://127.0.0.1:8000 ')
parser.add_argument('--nodeos', metavar='', help="Path to nodeos binary", default='../../build/programs/nodeos/nodeos ')
parser.add_argument('--keosd', metavar='', help="Path to keosd binary", default='../../build/programs/keosd/keosd ')
parser.add_argument('--log-path', metavar='', help="Path to log file", default='/mnt/d/Go/output.random.log')
parser.add_argument('-a', '--all', action='store_true', help="Do everything marked with (*)")

for (flag, command, function, inAll, help) in commands:
    prefix = ''
    if inAll: prefix += '*'
    if prefix: help = '(' + prefix + ') ' + help
    if flag:
        parser.add_argument('-' + flag, '--' + command, action='store_true', help=help, dest=command)
    else:
        parser.add_argument('--' + command, action='store_true', help=help, dest=command)

args = parser.parse_args()

args.symbol = 'SYS'
args.contract = 'producer111e'
args.contract2 = 'producer111v'


logFile = open(args.log_path, 'a')
logFile.write('\n\n' + '*' * 80 + '\n\n\n')

haveCommand = False
for (flag, command, function, inAll, help) in commands:
    if getattr(args, command) or inAll and args.all:
        if function:
            haveCommand = True
            function()
if not haveCommand:
    print('testtool.py: Tell me what to do. -a does almost everything. -h shows options.')