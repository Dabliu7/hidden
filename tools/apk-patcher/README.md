# apk-patcher

Adiciona a `libGTASA_Coop.so` ao **seu** APK do GTA: San Andreas. Nenhum arquivo do jogo é
distribuído por este projeto.

> **Estado:** a edição do smali foi testada com um trecho sintético; o fluxo completo ainda não foi
> validado com um APK real. Guarde o APK original.

## Requisitos (no PATH)

- Python 3.8+
- Java (traz o `keytool`)
- [apktool](https://apktool.org/)
- `zipalign` e `apksigner` (Android SDK Build-Tools)

## Uso

```bash
python patch_apk.py gta-sa.apk --libs gtasa-coop-android --out gta-sa-coop.apk
```

`--libs` é a pasta extraída de `gtasa-coop-<versão>-android.zip` (ou os artefatos do CI organizados
como `lib/armeabi-v7a/libGTASA_Coop.so` e `lib/arm64-v8a/libGTASA_Coop.so`).

## Instalação no aparelho

1. Faça backup dos saves.
2. Desinstale o jogo original (a assinatura do APK muda). Os dados em `Android/obb` devem ser mantidos
   ou copiados de volta.
3. Instale `gta-sa-coop.apk`.

## Iniciar uma sessão

Crie o arquivo `coop.txt` em `Android/data/<pacote do jogo>/files/` com uma linha:

```
host
```

ou

```
connect 192.168.0.10
```

Sem o arquivo, o jogo roda normalmente. O diagnóstico fica em `coop.log`, na mesma pasta.
