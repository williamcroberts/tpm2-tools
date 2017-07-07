#!/usr/bin/lua
require("tpm_shell")

s = tpm_open("--tcti", "tabrmd")

rc = createprimary(s,  '-A', 'o', '-g', '0xb', '-C', 'prim.ctx', '-G', '0x1')
--print(t["primCTX"]) --should be an input to create
rc,t = createpolicy(s, '-P', '-i', '0', '-g', '0x4', '-f', 'policy.file')
print(t["policy"]) --should be an input to create
rc = create(s, '-c', 'prim.ctx', '-g', '0xb', '-G', '0x1', '-L', 'policy.file', '-o', 'key.pub', '-O', 'key.priv')
--print(t["keyPub"], t["keyPriv"]) --should be an input to load
rc = load(s, '-c', 'prim.ctx', '-u', 'key.pub', '-r', 'key.priv', '-n', 'key.name', '-C', 'sec.ctx')
--print(t["keyName"], t["secCtx"]) --should be an input to rsaencrypt and rsadecrypt
rc = rsaencrypt(s, '-c', 'sec.ctx', '-I', 'plain.txt', '-o', 'plain.enc')
rc,t = createpolicy(s, '-P', '-i', '0', '-g', '0x4', '-r')
print(t["sessionHandle"]) --should be an input to rsadecrypt
rc = rsadecrypt(s, '-c', 'sec.ctx', '-I', 'plain.enc', '-o', 'plain.dec', '-Y')
--[===[
print("\n\n\nEXPECTED FAILURE SINCE WRONG PCRS - RESTART TPM and RM AFTER THIS CALL")
rc,t = createpolicy(s, '-P', '-i', '16', '-g', '0x4', '-r')
print(t["sessionHandle"]) --should be an input to rsadecrypt
rc = rsadecrypt(s, '-c', 'sec.ctx', '-I', 'plain.enc', '-o', 'plain.dec2', '-Y')
--]===]
tpm_close(s)
