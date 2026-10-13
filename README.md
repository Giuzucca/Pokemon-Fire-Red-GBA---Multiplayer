# Pokémon FireRed Multiplayer local

Modificação comunitária de Pokémon FireRed para duas instâncias independentes do mGBA se encontrarem no mapa e jogarem duelos PvP e batalhas cooperativas locais pela rede TCP. A conexão é direta entre os computadores; Hamachi pode criar a rede privada entre pessoas em locais diferentes. O projeto não oferece servidor público nem compartilha campanha, save, inventário ou equipe.

> **Este repositório não contém o jogo.** Não publique nem distribua ROM, BIOS, arquivos `.sav`/`.ss*` ou outros recursos protegidos. Cada jogador precisa obter legalmente sua própria cópia e preparar a ROM localmente. O patch e os scripts aqui servem para reproduzir a modificação; Pokémon e FireRed pertencem aos seus respectivos titulares. A base técnica é a descompilação comunitária [pret/pokefirered](https://github.com/pret/pokefirered), cujo código e instruções devem ser consultados no projeto original.

## Versões e downloads

As versões estão marcadas por tags no GitHub. Os links abaixo baixam o código-fonte de cada versão em ZIP; o arquivo `.patch` fica dentro de `patches/` no ZIP.

- [v6.0.0 — recomendada, baixar ZIP](https://github.com/Giuzucca/Pokemon-Fire-Red-GBA---Multiplayer/archive/refs/tags/v6.0.0.zip): corrige o índice usado ao consultar os iniciais aleatórios. As três Pokébolas mostram espécies diferentes.
- [v5.0.0 — versão anterior, baixar ZIP](https://github.com/Giuzucca/Pokemon-Fire-Red-GBA---Multiplayer/archive/refs/tags/v5.0.0.zip): inclui a escolha de iniciais aleatórios, mas pode repetir a mesma espécie nas Pokébolas. Está mantida como versão histórica para baixar e comparar.

Os downloads contêm código e patch, **não uma ROM pronta**. Para jogar, cada pessoa precisa fornecer sua própria ROM legal de FireRed (EUA, revisão 1.0) e compilar localmente conforme os passos abaixo. Não distribua ROM compilada, BIOS, saves ou save states.

## O que funciona

- Exploração simultânea em mapas diferentes, cada jogador na sua própria janela e campanha.
- Presença do outro jogador quando ambos estão no mesmo mapa; convite de duelo ao ficar ao lado, olhar para o amigo e apertar **A**. A outra pessoa aceita ou recusa na própria tela.
- Duelos individuais: cada jogador controla seu Pokémon, pode trocar e escolher golpes. Vitória, derrota e desistência retornam ao mapa; o duelo PvP não dá experiência e restaura as equipes ao estado anterior.
- Convite para cooperar em encontros selvagens e batalhas de treinadores suportadas. O alcance atual para encontro selvagem é de **10 blocos**. Cada pessoa escolhe e controla seus Pokémon; HP, PP, itens, captura e experiência são aplicados à campanha/proprietário correspondente.
- Cancelamento da espera com **B do GBA** (tecla **X** no mapeamento usado nos testes), com limite de 30 segundos emulados.
- No início da campanha, Oak pergunta se você prefere o modo clássico ou três iniciais aleatórios. No modo aleatório, as três Pokébolas recebem espécies válidas diferentes da Pokédex interna do FireRed; você vê o nome e o sprite de cada uma antes de confirmar.
- Validação de compatibilidade da ROM entre as duas pontas e reconexão após carregar save state, seguindo o passo a passo abaixo.

O visitante pode atravessar o personagem e alguns estados visuais especiais não são sincronizados. O jogo foi testado em duas instâncias locais do mGBA; a conexão Hamachi depende da rede/firewall de cada computador.

## O que há neste repositório

- `patches/pokemon-fire-red-multiplayer.patch`: alterações C/assembly aplicadas sobre uma revisão identificada de `pret/pokefirered`.
- `multiplayer/src/`: protocolo TCP e integração Lua com a API de scripting do mGBA.
- `multiplayer/tools/`: empacotador e scripts Windows para preparar pastas independentes de host e client.
- `multiplayer/tests/`: testes de protocolo e harness de integração com dois núcleos mGBA.

Não há ROM pronta, arquivo de save, save state ou executável do emulador aqui.

## Jogar a versão pronta no Windows

1. Instale o **mGBA 0.10.5** em cada computador. Use duas instalações/janelas independentes; não use a função de cabo/link do emulador.
2. Compile a ROM seguindo a seção “Compilar a ROM modificada” abaixo, ou obtenha de forma legítima uma ROM já modificada. Os dois jogadores devem usar exatamente a mesma compilação.
3. Na pasta `dist` gerada, execute no computador anfitrião:

   ```powershell
   .\Preparar-Jogadores.ps1 -IPHost 127.0.0.1
   ```

   Esse endereço serve para simular dois jogadores no mesmo computador. Para jogar à distância, use o IPv4 Hamachi do anfitrião (normalmente `25.x.x.x`) ou tente `-DetectHamachi`.

4. Para teste local, abra duas janelas do mGBA. Na primeira abra `dist\host\FireRed-MP-host.gba`; na segunda, `dist\client\FireRed-MP-client.gba`. Em cada janela, abra **Ferramentas/Tools → Scripting → Load script** e carregue o `Multiplayer.lua` da pasta correspondente. Carregue primeiro o script do host e depois o do client. O console Lua deve indicar amigo conectado.
5. Para dois computadores, conecte ambos à mesma rede Hamachi. O anfitrião executa `Preparar-Jogadores.ps1 -IPHost 25.x.x.x`, fecha/reabre as instâncias para carregar os scripts recém-gerados e envia ao amigo a pasta `dist` sem dados pessoais. O amigo abre a ROM/script `client`; o anfitrião abre `host`.
6. Permita o mGBA na rede privada do Windows Firewall quando solicitado. A porta padrão é **TCP 4096**; se a conexão falhar, confira conectividade Hamachi e uma regra de entrada TCP para essa porta no PC anfitrião. O script não altera o firewall automaticamente.
7. Comece um jogo em cada janela ou carregue os dois saves correspondentes. As campanhas são separadas; não copie o save de uma pessoa para a outra. Desative a pausa do mGBA quando a janela perde o foco para testar duas janelas no mesmo PC.

As teclas variam conforme a configuração do mGBA. No mapeamento usado durante os testes: **A = Z**, **B = X**. Clique na janela desejada antes de apertar a tecla.

### Convidar para duelo

Conectados e lado a lado no mesmo mapa, vire para o personagem do amigo e aperte **A**. Escolha **YES**. O convite aparece na tela do outro jogador; ele pode aceitar ou recusar. Se o convite expirar ou for cancelado, feche o diálogo e tente novamente. Os dois jogadores podem convidar; se enviarem ao mesmo tempo, cancelem e deixem apenas um enviar.

### Iniciar batalha cooperativa

Ao iniciar um encontro selvagem ou uma batalha de treinador compatível, um jogador pode escolher esperar pelo amigo. O parceiro deve estar conectado, no mesmo mapa e a até 10 blocos para o encontro selvagem; aceite o convite na própria tela. Cada jogador toma suas decisões de combate. Para cancelar a espera, aperte **X** no teclado (botão B do GBA, no mapeamento descrito). O limite de espera é de 30 segundos em tempo de jogo; pausar o emulador também pausa esse tempo.

## Save e save state

Cada janela precisa de seu próprio `.sav` e seu próprio save state. Os states do mGBA guardam a ROM e a memória daquela instância; não são sincronizados pelo Lua. Para retomar uma sessão, salve um state separado em cada janela no mesmo ponto, depois carregue o par correspondente. Use states criados com a mesma versão da ROM. Após carregar, aguarde a reconexão; no console Lua do client, `FRMP_CONNECT()` tenta conectar novamente. Se necessário, execute `FRMP_STATE_RECOVER()` em cada instância. O host mantém a escuta aberta.

States antigos podem reiniciar no título se forem incompatíveis com a compilação atual. Nesse caso, carregue o save normal `.sav` compatível e crie states novos. Mantenha cópias de segurança antes de trocar a versão da ROM. Nunca publique saves ou states pessoais.

## Compilar a ROM modificada

### Requisitos

- Windows 10/11 com WSL2 (Ubuntu ou Kali) ou Linux.
- Dependências e ferramentas de build indicadas em [`pret/pokefirered` — INSTALL.md](https://github.com/pret/pokefirered/blob/master/INSTALL.md), incluindo `git`, `make`, compilador ARM e Python 3.
- Uma cópia legal de **Pokémon FireRed (EUA), revisão 1.0**, identificada pelo SHA-1 `41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc`. Não use FireRed 1.1, tradução ou ROM hack como base.
- Para o teste completo: Lua 5.4 e biblioteca de desenvolvimento do mGBA (`libmgba`).

No terminal Linux/WSL, a partir da raiz deste repositório:

```sh
mkdir -p vendor
git clone https://github.com/pret/pokefirered.git vendor/pokefirered
git -C vendor/pokefirered checkout 037335f4c725d7c9aecdac87066f2002b4bd7e14
```

Copie sua ROM base legal para `vendor/pokefirered/baserom.gba`, depois aplique o patch e compile:

```sh
git -C vendor/pokefirered apply --check ../../patches/pokemon-fire-red-multiplayer.patch
git -C vendor/pokefirered apply ../../patches/pokemon-fire-red-multiplayer.patch
./multiplayer/tools/build.sh
```

O script executa `make -j4 all syms`, empacota a ROM e os scripts host/client em `dist/`, e executa os testes de protocolo e de duas instâncias. O teste com dois núcleos exige `libmgba`; se você só quer produzir a ROM, rode os passos de compilação/empacotamento manualmente:

```sh
make -C vendor/pokefirered -j4 all syms
python3 multiplayer/tools/package.py --source vendor/pokefirered
```

O empacotador precisa do arquivo de símbolos `pokefirered.sym` gerado pelo `make`. O resultado em `dist/FireRed-MP.gba` é gerado localmente e não deve ser enviado ao repositório público.

## Hamachi, IP e porta

Na pasta `dist`, no PC que será host:

```powershell
.\Preparar-Jogadores.ps1 -DetectHamachi
```

Se a detecção automática não encontrar exatamente um endereço, informe-o manualmente:

```powershell
.\Preparar-Jogadores.ps1 -IPHost 25.123.45.67 -Porta 4096
```

Copie a pasta `dist` atualizada para o computador client. Só o host precisa encaminhar a conexão TCP 4096 no Firewall do Windows para a interface Hamachi, se o Firewall bloquear o mGBA. Não configure port forwarding no roteador nem divulgue a porta na internet: a ideia é manter a ligação na rede privada Hamachi.

## Problemas comuns

- **“Amigo desconectado”**: carregue o script host primeiro, depois client; confirme o mesmo IP/porta e confira a rede Hamachi. No client, `FRMP_CONNECT()` tenta reconectar.
- **“ROM diferente”**: execute a ROM e o Lua gerados juntos pelo mesmo `dist`; não misture scripts de versões diferentes.
- **O outro jogador não aparece**: confirme que ambos estão conectados, no mesmo mapa/andar e que o mapa tem um espaço livre para o personagem visitante.
- **Não aparece convite de coop**: para encontros selvagens, deixe os jogadores no mesmo mapa e a até 10 blocos; para batalhas de treinador, nem todos os tipos/eventos do jogo foram validados.
- **Cancelar a espera**: clique na janela do jogador que enviou o convite e aperte **X** (botão B). Confirme o mapeamento do botão B nas configurações do mGBA.
- **State volta ao título**: use um save state criado com a mesma ROM compilada. Caso não funcione, carregue o save normal daquela campanha e crie um state novo.

## Base e atribuição

O patch foi criado sobre `pret/pokefirered` no commit `037335f4c725d7c9aecdac87066f2002b4bd7e14`. Consulte o projeto original para as instruções, créditos, código de terceiros e condições aplicáveis. Este repositório é uma modificação independente e não é afiliado ou endossado pela Nintendo, Game Freak, The Pokémon Company ou pret. O software é fornecido como está; faça backups dos saves.
