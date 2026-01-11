# TUIGame

## Brainstorm

J'ai déjà la base suivante de mécaniques suivantes :
- Le jeu se déroule dans un terminal type unix à travers des commandes
- Au premier démarrage du jeu, l'utilisateur est uniquement sur la machine1
- Cette machine est connecté à un réseau et peut le joueur peut se déplacer sur d'autres machines dont il connait l'ip
- Il existe plusieurs réseaux et donc certaines machines ne sont accessibles que par rebond

Concernant le gameplay j'ai les idées en vrac suivantes :
- Le chef du joueur envoie des mails qui lui indique des tâches à faire (aller sur le serveur X et regarder les logs, relancer la machine Y etc)
- Le joueur réalise les actions et doit répondre au chef par un mot de passe qu'il peut trouver en réalisant l'action demandé dans le style d'un CTF.
- J'aimerais faire en sorte qu'en parallèle des missions demandés par le chef, un mystère soit à découvrir par le joueur. Ce mystere serait la vraie trame principale du jeu et les mails uniquement un moyen de guider le joueur et de lui faire sentir que quelque chose d'étrange se passe sur les machines

- j'ai déjà implémenter des commandes unix-type (ls, cd, rm, etc) mais c'est un sous-ensemble très simplifié et avec des commandes inventés en plus pour correspondre au gameplay.
- Jeu grand public, je n'ai pas envie que les joueurs soient des dev à l'avance donc cela doit rester simple. Le terminal sert juste de cadre
- pas d'echec possible, juste de l'exploration

- Je veux que le joueur puisse techniquement réaliser tout le jeu dès le début sans indice s'il connait déjà les solutions. L'expérience vient par la découverte plutot que par des murs bloquants
- Je veux une progression semi-ouverte. C'est à dire que les mails arrivent au fur et à mesure pour donner des indications sur où aller mais que cela ne soit pas obligatoire
- Une machine dispose d'un certain nombre de commandes par défaut mais l'utilisateur peut copier des binaires d'une machine à l'autre pour réaliser de nouvelles actions

- Logs, outils (ping et netsan)
- Le joueur doit se faire le réseau sur papier mais je mets à disposition un outil qui permet de sauter directement sur une machine où on est déjà connecté par simplicité de gameplay
- Les rebonds servent aux puzzle

- Je voudrais qu'il y est des traces un peu partout
- Résoudre le mystere serait l'ending du jeu


## Demo d'1h

### Phase 0

Début sur machine1. 

Premier mail : "Quelqu'un a essayé de s'infiltrer sur notre réseau depuis l'extérieur. Connecte toi à la machine 2 et donne moi l'adresse IP du suspect pour que l'on puisse la bloquer".

Solution : se rendre sur la machine2 puis regarder les logs dans /logs/auth-19670112.log. On voit plein d'adresses IP mais vers la fin on se rend compte qu'une revient beaucoup plus mais en échec. La derniere instance est un succès.
