# Addon de teste (protocolo Stremio)

Arquivos estáticos que simulam um addon para testar a Fase 4 sem depender de terceiros.
Os vídeos são filmes abertos da Blender Foundation (Creative Commons).

```bash
python3 -m http.server 7000 --bind 0.0.0.0 --directory tools/test-addon
```
No `.env` (ou em `addons` do `config.json`): `addons=http://<IP_DO_PC>:7000`

| Título no catálogo | ID | Streams |
| :--- | :--- | :--- |
| Sintel (2010) | `tt1727587` | MKV 1080p com legendas PT/EN externas; trailer MP4 com `proxyHeaders`; um torrent (deve ser ignorado) |
| Big Buck Bunny (2008) | `tt1254207` | HLS 1080p |
| Breaking Bad T1E1 | `tt0903747:1:1` | vídeo de teste (mapeamento de episódio) |
