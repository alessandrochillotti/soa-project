# Progetto di Sistemi Operativi Avanzati
## Alessandro Chillotti (mat. 0299824)
## Montaggio del modulo
Per generare il *kernel object* dal file sorgente è necessario spostarsi all'interno della directory `driver` e digitare il seguente comando.
```
make all
```
Per montare il modulo nel kernel Linux è necessario eseguire il seguente comando.
```
sudo insmod multi_flow_dev
```
Per smontare il modulo dal kernel Linux è necessario eseguire il seguento comando.
```
sudo rmmod multi_flow_dev
```

## Creazione del device file
Per creare il device file bisogna digitare il seguente comando:
```
mknod path c major minor
```
dove:
- `path` è il nome del file;
- `major` è l'identificato del driver, visibile digitando il comando `dmesg` come stampa ottenuta una volta montato il modulo;
- `minor` è l'identificativo del device file e può variare da 0 a 127.

## Avvio applicazione user
All'interno della directory `user` si può utilizzare `make` per ottenere il file eseguibile e, una volta prodotto, si può avviare l'applicazione con il seguente comando.
```
sudo ./user path major minor
```

## Utilizzo dei parametri del modulo
Sono stati implementati quattro script con lo scopo di facilitare l'interazione con i parametri definiti.

### Parametro di abilitazione del multi-flow device file
Per questo parametro sono stati progettati due script:
- Lo script [enabled_set.sh](utils/enabled_set.sh) permette di settare l'abilitazione di un determinato device file.
```
sudo bash enabled_set.sh MINOR
```
- Lo script [enable_query.sh](utils/enabled_query.sh) permette di capire se il multi-flow device file è abilitato o meno per un certo minor.
```
sudo bash enabled_set.sh MINOR PRIORITY
```
dove PRIORITY è un valore che può essere 'Y' o 'N'.
### Parametro di conteggio dei byte nei buffer
Per questo parametro è stato implementato lo script [byte_query.sh](utils/byte_query.sh) che permette di capire il numero di byte presenti nei due flussi associati al multi-flow device file.
```
sudo bash byte_query.sh MINOR PRIORITY
```
### Parametro di conteggio dei thread in attesa
Per questo parametro è stato implementato lo script [thread_query.sh](utils/thread_query.sh) che permette di capire il numero di thread che attengono dati sul loro flusso.
```
sudo bash thread_query.sh MINOR PRIORITY
```