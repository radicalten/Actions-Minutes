"""Scored categories describe the tested/source ISA, never the ARM call wrapper."""
import re
NAMES=('ARM7','ARM7Thumb','ARM9','ARM9Thumb')
def classify(v):
 n=v['name']
 if '_return_' in n:
  return 'ARM','ARM exception-return instruction; target may be Thumb'
 if n.startswith(('cpu_thumb_','cpu_arm7_thumb_','cpu_arm9_thumb_')):
  return 'THUMB','Thumb instruction under test'
 if re.search(r'_irq_(entry|nested)_thumb$',n):
  return 'THUMB','Hardware IRQ from Thumb source state'
 if '_raw_' in n and '_thumb_' in n:
  return 'THUMB','Raw exception from Thumb source state'
 assert n.startswith(('cpu_arm_','cpu_arm7_','cpu_arm9_')),n
 return 'ARM','ARM instruction or ARM-source system test'
def category(cpu,isa):return f'ARM{cpu}'+('Thumb' if isa=='THUMB' else '')
