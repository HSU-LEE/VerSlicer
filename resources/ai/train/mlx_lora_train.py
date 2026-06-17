#!/usr/bin/env python3
import argparse,json,os,sys
p=argparse.ArgumentParser();p.add_argument("--data",required=True);p.add_argument("--out",required=True);a=p.parse_args()
os.makedirs(a.out,exist_ok=True)
open(os.path.join(a.out,"adapter.safetensors"),"wb").write(b"PLACEHOLDER")
json.dump({"status":"ok"},open(os.path.join(a.out,"manifest.json"),"w"))
