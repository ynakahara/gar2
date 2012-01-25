#!/usr/bin/python
import re
import sys

class Summary:
  def __init__(self, kind, name):
    self.kind = kind
    self.name = name
    self.linesExecuted = None
    self.branchesExecuted = None
    self.takenAtLeastOnce = None
    self.callsExecuted = None

def summarize(fname):
  summary = None
  summaries = []
  for l in open(fname):
    l = l.strip()
    m = re.match("File '(.*)'", l)
    if m:
      summary = Summary("file", m.group(1))
      summaries.append(summary)
      continue
    m = re.match("Function '(.*)'", l)
    if m:
      summary = Summary("func", m.group(1))
      summaries.append(summary)
      continue
    m = re.match("Lines executed:(.*)% of .*", l)
    if m:
      summary.linesExecuted = float(m.group(1))
      continue
    m = re.match("Branches executed:(.*)% of .*", l)
    if m:
        summary.branchesExecuted = float(m.group(1))
        continue
    m = re.match("Taken at least once:(.*)% of .*", l)
    if m:
      summary.takenAtLeastOnce = float(m.group(1))
      continue
    m = re.match("Calls executed:(.*)% of .*", l)
    if m:
      summary.callsExecuted = float(m.group(1))
      continue
  return summaries

def ratio_to_str(x):
  if x is None:
    return '   N/A'
  else:
    return '%6.2f' % x

def pad_to_len(s, n):
  if len(s) < n:
    return s + (' ' * (n - len(s)))
  else:
    return s

def print_summaries(summaries):
  n = 2 + max([len(s.name) for s in summaries])
  print(' ' * (5 + n) + '%line   %branch   %taken   %call')
  for summary in summaries:
    ss = []
    ss.append(pad_to_len(summary.kind, 5))
    ss.append(pad_to_len(summary.name, n))
    ss.append(pad_to_len(ratio_to_str(summary.linesExecuted), 8))
    ss.append(pad_to_len(ratio_to_str(summary.branchesExecuted), 10))
    ss.append(pad_to_len(ratio_to_str(summary.takenAtLeastOnce), 9))
    ss.append(pad_to_len(ratio_to_str(summary.callsExecuted), 8))
    print(''.join(ss))

print_summaries(summarize('/dev/stdin'))
