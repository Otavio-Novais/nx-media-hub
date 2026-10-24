# NX Media Hub

Homebrew `.nro` de uso pessoal para Nintendo Switch (Atmosphère) que junta, numa interface feita para o controle:

- **Início:** Continuar assistindo, Favoritos e Canais favoritos.
- **Filmes e Séries:** catálogo do TMDB em português (destaques, busca, detalhes, temporadas e episódios).
- **Fontes via addons do Stremio:** "Assistir" consulta os addons configurados e lista as fontes de vídeo.
- **TV ao Vivo:** listas `.m3u` / `.m3u8` com categorias e logos.
- **Player:** libmpv com decodificação por hardware do Tegra (`nvtegra-copy`), troca de áudio/legenda e legendas externas; retoma de onde parou.
- **Configurações:** chave do TMDB, addons e listas M3U editáveis no próprio Switch.

Versão atual: **0.5.2**. O `.nro` dos [releases](https://github.com/Otavio-Novais/nx-media-hub/releases) traz só a
**TV ao Vivo** (Filmes e Séries ainda em desenvolvimento, veja [Build](#build)): copie para `sdmc:/switch/` e abra pelo
Homebrew Menu.

Diretrizes, decisões e pendências conhecidas: [`docs/diretrizes.md`](docs/diretrizes.md).

| Fase | Conteúdo | Estado |
| :--- | :--- | :--- |
| 0 | Estrutura, Borealis, build Switch (Docker) e desktop | ✅ |
| 1 | Player libmpv + OSD | ✅ |
| 2 | TV ao vivo (M3U) + `config.json` | ✅ |
| 3 | Catálogo TMDB | ✅ |
| 4 | Addons Stremio + seletor de fontes + legendas | ✅ |
| 5 | Início, histórico, favoritos, tela de configurações e ícone | ✅ |

---

## Controles

| Onde | Botão | Ação |
| :--- | :--- | :--- |
| Início | **Y** | Remover do histórico / dos favoritos |
| Filmes e Séries | **Y** / **X** | Buscar / voltar para "Em alta" |
| TV ao Vivo | **X** / **Y** | Recarregar as listas M3U / favoritar o canal |
| Detalhes | **A** em "Assistir" ou num episódio | Buscar fontes nos addons |
| Detalhes | **X** | Favoritar / desfavoritar |
| Player | **A** | Pausar / continuar |
| Player | **◀ ▶** ou **L R** | Voltar / avançar 10 s |
| Player | **X** / **Y** | Faixa de áudio / legenda |
| Player | **B** | Sair do vídeo |
| Qualquer tela | **+** | Fechar o app |

---

## Configuração (`config.json`)

Na primeira execução o app cria `sdmc:/switch/nx-media-hub/config.json` (no desktop: `~/.config/nx-media-hub/config.json`):

```json
{
  "tmdbApiKey": "",
  "addons": [],
  "m3uUrls": ["https://raw.githubusercontent.com/Otavio-Novais/nx-media-hub-lists/main/lista_limpa.m3u"],
  "countries": []
}
```

| Campo | O que é |
| :--- | :--- |
| `tmdbApiKey` | Chave da API do TMDB (v3), gratuita em themoviedb.org → Configurações → API. Necessária para o catálogo. |
| `addons` | Lista de addons do Stremio (veja abaixo). |
| `m3uUrls` | Listas de canais: URLs ou arquivos no SD (ex.: `sdmc:/switch/nx-media-hub/lista.m3u`). Padrão: a [lista limpa](https://github.com/Otavio-Novais/nx-media-hub-lists), com os canais em português, esportes e notícias do [iptv-org](https://github.com/iptv-org/iptv), sem repetidos e sem os fora do ar (verificada a cada 6 horas pelo GitHub Actions). As listas são lidas enquanto baixam (o arquivo nunca fica inteiro na memória), até **50 mil canais** no total: passando disso, o resto é ignorado e o app avisa. |
| `countries` | Opcional. Mostra só canais desses países, pelo `tvg-id` das listas do iptv-org (`Canal.<país>@<versão>`), e esconde versões regionais de outros países (`@DK`, `@Poland`…). Ex.: `["br","pt","us","uk","ca"]` (o iptv-org usa `uk` para o Reino Unido). Vazio = todos. |

### Pela aba Configurações

A chave do TMDB, os addons e as listas M3U podem ser editados no próprio Switch (teclado na tela; **A** num item abre a opção de
apagar). As mudanças são gravadas no `config.json` na hora. A TV ao Vivo recarrega sozinha quando as
listas mudam e avisa quando alguma delas não carrega.
O filtro `countries` só é editado pelo arquivo.

### Pelo arquivo

Para editar pelo PC sem tirar o cartão: no Switch abra o **DBI → Run MTP responder**, conecte o cabo USB e edite o arquivo. O `config.json` é lido quando o app abre: **feche e abra o app depois de editar**. Se o JSON estiver inválido, o app usa os padrões e **não** sobrescreve seu arquivo.

### Histórico e favoritos

Ficam na mesma pasta, em `history.json` (Continuar assistindo, com a posição de cada vídeo, salva a cada 30 s) e
`favorites.json` (títulos e canais). A gravação é atômica: se o app fechar no meio, o arquivo anterior é recuperado.

---

## Como adicionar fontes (addons do Stremio)

O app fala o mesmo protocolo HTTP dos addons do Stremio: para um filme ou episódio ele pede
`{addon}/stream/movie/{imdb}.json` ou `{addon}/stream/series/{imdb}:{temporada}:{episódio}.json`
e mostra as fontes devolvidas no seletor.

### 1. Pegue a URL do addon

Use a URL do **manifest** que o próprio addon mostra (a mesma que você colaria no Stremio):

```
https://exemplo-de-addon.com/manifest.json
```

- Addons **configuráveis** geram uma URL própria na página de configuração (com suas opções no caminho, ex.
  `https://addon.com/<config>/manifest.json`): copie essa URL inteira.
- Links `stremio://...` também funcionam: o app troca para `https://`.
- O app aceita com ou sem o `/manifest.json` no final.

### 2. Coloque no `config.json`

```json
"addons": [
  "https://exemplo-de-addon.com/manifest.json",
  "https://outro-addon.com/<config>/manifest.json"
]
```

Feche e abra o app. Ao apertar **Assistir**, todos os addons da lista são consultados, um depois do outro, e as
fontes são juntadas num único seletor ("nome - descrição da fonte", sem emojis). A ordem prioriza fontes já no
cache do debrid (`[RD+]` no Torrentio), depois dublado/dual áudio e então a resolução (1080p > 720p > 4K, que pesa
mais para o Switch).

### 3. Requisitos para um addon funcionar

O player só toca **links diretos** `http(s)` (MP4, MKV, HLS `.m3u8`...). Por isso:

| O addon devolve... | Funciona? |
| :--- | :--- |
| `url` com link direto http(s) | ✅ Sim |
| `url` + `behaviorHints.proxyHeaders.request` (User-Agent, Referer...) | ✅ Sim, os cabeçalhos são enviados pelo player |
| `subtitles` (legendas externas `.srt`/`.vtt`) | ✅ Sim, convertidas para UTF-8 e com português selecionado automaticamente |
| Só `infoHash` (torrent) | ❌ Ignorado (não há cliente de torrent no app) |
| `ytId` (YouTube) ou `externalUrl` (abrir no navegador) | ❌ Ignorado |

Também é preciso que o addon responda por **IDs do IMDb** (`tt1234567`), que é o que o catálogo do TMDB fornece.

### 4. Testando um addon

Abra no navegador do PC, trocando pela URL do seu addon (sem o `/manifest.json`):

```
https://exemplo-de-addon.com/stream/movie/tt1727587.json
```

Se aparecer `{"streams": [...]}` com entradas que têm `"url": "https://..."`, ele vai funcionar no app.
Os logs do app (`nxlink -s`, veja abaixo) mostram cada consulta: `addon <url>: N streams for ...` ou o erro.

### 5. Criando o seu próprio addon (arquivos estáticos)

Um addon pode ser só uma pasta de JSONs servida por HTTP. O projeto traz um de exemplo em
[`tools/test-addon/`](tools/test-addon/) com filmes abertos da Blender Foundation:

```bash
python3 -m http.server 7000 --bind 0.0.0.0 --directory tools/test-addon
```

e no `config.json`: `"addons": ["http://<IP_DO_PC>:7000"]`. Para adicionar um título, crie
`stream/movie/<imdb>.json` (ou `stream/series/<imdb>:<temporada>:<episódio>.json`) no formato:

```json
{
  "streams": [
    {
      "name": "Minha fonte\n1080p",
      "description": "Dublado",
      "url": "https://servidor/video.m3u8",
      "behaviorHints": { "proxyHeaders": { "request": { "Referer": "https://servidor/" } } },
      "subtitles": [{ "id": "pt", "lang": "por", "url": "https://servidor/legenda.srt" }]
    }
  ]
}
```

Formato completo: [documentação do Stremio Addon SDK](https://github.com/Stremio/stremio-addon-sdk/blob/master/docs/api/responses/stream.md).

---

## Build

### Nintendo Switch (Docker)

```bash
git clone --recursive https://github.com/Otavio-Novais/nx-media-hub.git   # ou: git submodule update --init --recursive
docker run --rm -v "$(pwd):/data" devkitpro/devkita64:20251117 bash /data/scripts/build_switch.sh
# saída: cmake-build-switch/nx-media-hub.nro
```

Para o release, Filmes e Séries (catálogo, fontes e as opções de TMDB/addons nas Configurações) ficam ocultos:
acrescente `-DENABLE_MOVIES=OFF` ao fim do comando acima. Nesse modo o `.env` é ignorado, então nenhuma chave vai
para o `.nro`. Gere o release a partir de um checkout limpo.

O script instala o ffmpeg (com `nvtegra`) e o libmpv pré-compilados pelo projeto [wiliwili](https://github.com/xfangfang/wiliwili).

### Desktop Linux (desenvolvimento)

Dependências: `pkg-config libwayland-dev wayland-protocols libxkbcommon-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libdbus-1-dev libmpv-dev libcurl4-openssl-dev`

```bash
cmake -B build-desktop -DPLATFORM_DESKTOP=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-desktop -j$(nproc)
./build-desktop/nx-media-hub -d            # -p <url> abre o player direto
ctest --test-dir build-desktop               # testes (M3U, TMDB, addons, codificação de texto, histórico/favoritos)
```

### Padrões de desenvolvimento (`.env`)

Um `.env` na raiz (ignorado pelo git) define valores usados quando o `config.json` os deixa vazios:

```
tmdbApiKey=<sua chave>
addons=http://192.168.0.10:7000,https://outro-addon.com/manifest.json
```

Eles são embutidos no binário no build: **não distribua um `.nro` gerado com `.env`**.

---

## Enviar ao Switch e ver os logs

1. No Switch: abra o Homebrew Menu **em modo título** (segure **R** ao abrir um jogo) e aperte **Y** (NetLoader).
2. No PC:

```bash
docker run --rm -it --network host -v "$(pwd):/data" devkitpro/devkita64:20251117 \
  nxlink -s -a <IP_DO_SWITCH> /data/cmake-build-switch/nx-media-hub.nro
```

O `-s` mostra os logs do app no terminal (URLs com chaves de API aparecem mascaradas). Não teste a porta do NetLoader com outras conexões antes de enviar:
isso trava o envio. Alternativa: copie o `.nro` para `sdmc:/switch/` (DBI/MTP ou cartão SD).

---

## Estrutura

```
include/ source/
  api/      HttpClient (curl), TmdbClient, AddonClient, M3uParser (+ parsers puros testáveis)
  core/     Config (config.json), Library/Storage/AppData (histórico e favoritos), Background (threads),
            TextEncoding (legendas → UTF-8)
  models/   StreamSource, MediaItem/Channel/Episode...
  player/   MpvCore (libmpv + OpenGL), VideoView (OSD), PlayerActivity
  views/    Abas (Início, Catálogo, TV ao Vivo, Configurações), DetailView, SourcePicker, PosterCard, RemoteImage
resources/  XML das telas, traduções (pt-BR/en-US), fontes e ícones
tests/      testes de host + fixtures
tools/      addon de teste
library/    borealis (submódulo, fork xfangfang)
```

Agradecimentos: [Borealis (xfangfang)](https://github.com/xfangfang/borealis) e [wiliwili](https://github.com/xfangfang/wiliwili), cujo player serviu de referência.
