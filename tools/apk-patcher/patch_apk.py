#!/usr/bin/env python3
"""Adiciona a libGTASA_Coop.so ao SEU APK do GTA: San Andreas.

O que o script faz:
  1. desmonta o APK com o apktool;
  2. copia lib/<abi>/libGTASA_Coop.so para cada ABI que o APK ja possui;
  3. insere System.loadLibrary("GTASA_Coop") logo apos o loadLibrary("GTASA");
  4. remonta, alinha (zipalign) e assina (apksigner) com uma chave local.

Requisitos no PATH: java, apktool, zipalign, apksigner, keytool
(zipalign/apksigner vem no Android SDK Build-Tools; keytool vem com o Java).

Uso:
  python patch_apk.py gta-sa.apk --libs gtasa-coop-android --out gta-sa-coop.apk

`--libs` e a pasta extraida do pacote de release, contendo lib/<abi>/libGTASA_Coop.so.
O APK gerado tem assinatura diferente da original: desinstale o jogo antes de
instalar (faca backup dos saves) e mantenha os dados/OBB.
"""
import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

LIB_NAME = "libGTASA_Coop.so"
LOAD_RE = re.compile(
    r'(?P<indent>[ \t]*)const-string(?:/jumbo)? (?P<reg>[vp]\d+), "GTASA"\s*\n'
    r'(?:[ \t]*\n)*'
    r'[ \t]*invoke-static \{(?P=reg)\}, Ljava/lang/System;->loadLibrary\(Ljava/lang/String;\)V[^\n]*\n'
)


def patch_smali_text(text: str):
    """Devolve (novo_texto, quantidade_de_insercoes). Idempotente."""
    if '"GTASA_Coop"' in text:
        return text, 0

    def add(match):
        indent, reg = match.group("indent"), match.group("reg")
        return (
            match.group(0)
            + f'\n{indent}const-string {reg}, "GTASA_Coop"\n\n'
            + f"{indent}invoke-static {{{reg}}}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V\n"
        )

    return LOAD_RE.subn(add, text)


def tool(name: str) -> str:
    path = shutil.which(name) or shutil.which(name + ".bat") or shutil.which(name + ".cmd")
    if not path:
        sys.exit(f"erro: '{name}' nao encontrado no PATH (veja os requisitos no topo do script).")
    return path


def run(*cmd):
    print(">", " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("apk", type=Path, help="APK original do jogo (o seu)")
    ap.add_argument("--libs", type=Path, required=True, help="pasta com lib/<abi>/libGTASA_Coop.so")
    ap.add_argument("--out", type=Path, default=Path("gta-sa-coop.apk"))
    ap.add_argument("--keystore", type=Path, default=Path("coop-debug.keystore"),
                    help="chave de assinatura (criada se nao existir)")
    args = ap.parse_args()

    if not args.apk.is_file():
        sys.exit(f"erro: APK nao encontrado: {args.apk}")
    apktool, zipalign, apksigner, keytool = (tool(n) for n in ("apktool", "zipalign", "apksigner", "keytool"))

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp) / "apk"
        run(apktool, "d", "-f", "-o", work, args.apk)

        # 2. bibliotecas: somente para as ABIs que o APK ja tem
        lib_root = work / "lib"
        abis = [d.name for d in lib_root.iterdir() if (d / "libGTASA.so").is_file()] if lib_root.is_dir() else []
        if not abis:
            sys.exit("erro: o APK nao contem lib/<abi>/libGTASA.so (APK dividido? use o APK base completo).")
        for abi in abis:
            src = args.libs / "lib" / abi / LIB_NAME
            if not src.is_file():
                sys.exit(f"erro: falta {src} (o pacote de release tem essa ABI?)")
            shutil.copy2(src, lib_root / abi / LIB_NAME)
            print(f"biblioteca adicionada para {abi}")

        # 3. smali
        patched = 0
        for smali in work.glob("smali*/**/*.smali"):
            text = smali.read_text(encoding="utf-8", errors="surrogateescape")
            if '"GTASA"' not in text:
                continue
            new, n = patch_smali_text(text)
            if n:
                smali.write_text(new, encoding="utf-8", errors="surrogateescape")
                patched += n
                print(f"loadLibrary inserido em {smali.relative_to(work)}")
        if not patched:
            sys.exit('erro: nao encontrei System.loadLibrary("GTASA") no APK; nada foi alterado.')

        # 4. remonta, alinha, assina
        unsigned, aligned = Path(tmp) / "unsigned.apk", Path(tmp) / "aligned.apk"
        run(apktool, "b", "-o", unsigned, work)
        run(zipalign, "-f", "-p", "4", unsigned, aligned)
        if not args.keystore.exists():
            run(keytool, "-genkeypair", "-keystore", args.keystore, "-storepass", "android", "-keypass", "android",
                "-alias", "coop", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
                "-dname", "CN=GTASA Coop local")
        run(apksigner, "sign", "--ks", args.keystore, "--ks-pass", "pass:android", "--key-pass", "pass:android",
            "--ks-key-alias", "coop", "--out", args.out, aligned)

    print(f"\npronto: {args.out}  (ABIs: {', '.join(abis)})")


if __name__ == "__main__":
    main()
