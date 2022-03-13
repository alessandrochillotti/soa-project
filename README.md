# Progetto di Sistemi Operativi Avanzati
## Alessandro Chillotti (mat. 0299824)
### Montaggio del modulo
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

### Creazione del device file
Per creare il device file bisogna digitare il seguente comando:
```
mknod path c major minor
```
dove:
- `path` è il nome del file;
- `major` è l'identificato del driver, visibile digitando il comando `dmesg` come stampa ottenuta una volta montato il modulo;
- `minor` è l'identificativo del device file e può variare da 0 a 127.

### Avvio applicazione user
All'interno della directory `user` si può utilizzare `make` per ottenere il file eseguibile e, una volta prodotto, si può avviare l'applicazione con il seguente comando.
```
sudo ./user path major minor
```