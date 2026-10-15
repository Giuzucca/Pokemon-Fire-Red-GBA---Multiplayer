# Pokémon FireRed Multiplayer v9

Dois jogadores, cada um em seu mGBA e com campanha própria, podem se encontrar no mapa, convidar para duelos e cooperar em encontros selvagens ou batalhas de treinadores. A conexão é direta pela rede local ou por uma rede privada Hamachi; não há servidor público.

## O que baixar

Baixe o ZIP desta versão pelo GitHub e extraia tudo em uma pasta. O repositório contém o patch e os scripts Lua, mas não contém a ROM original, saves ou o emulador. Cada jogador precisa de sua própria cópia de **Pokémon FireRed USA, revisão 1.0**, com SHA-1 `41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc`. Traduções, FireRed 1.1 e outras ROM hacks não servem como base.

Instale o [mGBA 0.10.5](https://mgba.io/downloads.html) com suporte a Lua nos dois computadores. Se o menu **Tools/Ferramentas → Scripting** não existir, essa instalação do mGBA não tem o recurso necessário.

## Preparar para jogar no mesmo computador

Abra o PowerShell na pasta extraída e execute, substituindo o caminho pela localização da sua ROM original:

```powershell
.\Preparar-Jogadores.ps1 -ROMBase "C:\Jogos\FireRed-USA-1.0.gba" -IPHost 127.0.0.1
```

O caminho entre aspas é apenas um exemplo. Confira o local real do arquivo `.gba` no Explorador de Arquivos e use **Copiar como caminho**. O arquivo precisa existir; `mGBA.exe` é o emulador, não a ROM do jogo.

O preparador confere a ROM original, aplica `FireRed-MP-v9.ips`, verifica o resultado e cria pastas separadas `host` e `client`. Ele preserva saves existentes. Não misture ROM ou Lua de versões diferentes.

1. Abra `host\FireRed-MP-host.gba` na primeira janela do mGBA. Em **Tools/Ferramentas → Scripting → Load script**, carregue `host\Multiplayer.lua`.
2. Abra `client\FireRed-MP-client.gba` na segunda janela e carregue `client\Multiplayer.lua`.
3. O console Lua deve mostrar a conexão. Use um save diferente em cada janela. Para controlar as duas no mesmo PC, desative a pausa quando a janela perde foco nas configurações do mGBA.

No mapeamento usado nos testes, **A = Z** e **B = X** no teclado. Confira seu próprio mapeamento em **Tools → Settings → Controllers**.

## Jogar em dois computadores com Hamachi

1. Conecte os dois computadores à mesma rede Hamachi.
2. No computador host, execute `.\Preparar-Jogadores.ps1 -ROMBase "C:\Jogos\FireRed-USA-1.0.gba" -DetectHamachi`. Se não houver um único IPv4 Hamachi detectável, use `-IPHost 25.x.x.x` com o endereço do host.
3. Envie ao amigo o **mesmo ZIP desta versão**. No computador dele, execute `.\Preparar-Jogadores.ps1 -ROMBase "C:\Jogos\FireRed-USA-1.0.gba" -IPHost 25.x.x.x`, usando o IPv4 do host.
4. O host abre os dois arquivos da pasta `host`; o amigo abre os dois da pasta `client`. Inicie o Lua do host primeiro e depois o do client. Se o Firewall perguntar, permita o mGBA na rede privada. A porta usada é **TCP 4096**.

Cada pessoa mantém seu próprio `.sav`. Não copie saves ou save states para o repositório nem misture os arquivos dos jogadores. Save states de outra versão da ROM podem falhar; prefira o save normal ao atualizar.

## No jogo

- Para **duelar**, fiquem lado a lado no mesmo mapa. Olhe para o personagem do amigo e pressione **A**; escolha **YES**. O outro jogador aceita ou recusa na própria tela. Ambos podem convidar.
- Para **cooperar**, inicie um encontro selvagem ou fale com um treinador compatível quando o amigo estiver próximo no mesmo mapa. Um jogador escolhe aguardar e o outro aceita. **B** cancela a espera. Convites no mapa expiram após dois minutos de tempo emulado; o avanço rápido também acelera esse prazo.
- No começo de uma nova campanha, Oak oferece o modo clássico ou **três iniciais aleatórios distintos**, um em cada Pokébola.
- Em batalha dupla, cada jogador recebe sua própria experiência. A barra visual de XP não aparece nessa tela compacta; a experiência e mudanças de nível continuam funcionando.

As campanhas continuam independentes. Revanche cooperativa contra treinador já derrotado é permitida, e um treinador com apenas um Pokémon recebe uma cópia temporária desse Pokémon para a batalha dupla.

## Se algo não funcionar

- **ROM incompatível:** confira o SHA-1 da ROM original com `Get-FileHash "C:\Jogos\FireRed-USA-1.0.gba" -Algorithm SHA1`.
- **Lua diz ROM diferente:** carregue a ROM e o `Multiplayer.lua` gerados pelo mesmo preparador e pela mesma v9 nos dois lados.
- **Sem conexão:** confira o IP Hamachi do host, a porta TCP 4096, o Firewall e se o Lua do host começou antes do Lua do client.
- **Amigo invisível ou convite ausente:** os dois devem estar conectados no mesmo mapa/andar e próximos. Alguns eventos especiais não aceitam cooperação.
- **State antigo volta ao título:** carregue o `.sav` da campanha nessa versão e crie um state novo.

Modificação baseada na [descompilação comunitária pret/pokefirered](https://github.com/pret/pokefirered). Pokémon e FireRed pertencem aos seus respectivos titulares. O patch não inclui a ROM original.
