/*** inludes start ***/

#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h> 
#include <sys/ioctl.h>
#include <sys/types.h> 
#include <time.h>
#include <stdarg.h>

/*** inludes end ***/

/*** defines start ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
//queste macro sono qui per compatibilità con sistemi più vecchi e sistemi BSD

#define TEIC_VERSION "0.1.0"
#define TEIC_TAB 4

#define CTRL_KEY(k) ((k) & 0x1f)
//faccio l'AND tra k e 0x1f(0001 1111) e lo uso perchè così ogni numero a 8 bit (ASCII quindi) ha i primi 3 bit
//azzerati, questo mi permetto di usare i codici di controllo(che vanno da 0 a 31) senza fatica richiamando la macro
//e dandole in pasto un char

#define TBUF_INIT {NULL,0} //inizializzo il buffer che userò in renderUI()


/***defines end***/

/***data start***/

typedef struct erow //editor rows, la usiamo per salvare una linea di testo
{
	int size;
	int rsize;//contiene la dimemsione delle cose contenute in render
	char *chars;
	char *render;//contiene i caratteri da generare a schermo 
}erow;

struct editorConfig 
{
	int cx, cy; //cursor position x,y
	int rx; //render index for tabs should be > cx if no tabs on the line
    int rowoff; //row offset
    int coloff; //collumn offset

    int screenrows;
    int screencols;

    int numrows;

    erow *row;		

	char *filename;

	char statusmsg[80];
	time_t statusmsg_time;

    struct termios orig_termios;
};

struct tBuf//buffer per fare tutti i write() delle tilde insieme, invece che una alla volta
{
	char *buf;
	int lenght;
};

struct editorConfig E;

enum arrows
{
	ARROW_LEFT = 1000,//1000 perchè sta fuori dal range dei char
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DELETE,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

/*** data end ***/

/*** terminal start ***/

void error(const char *s)//handlign del messaggio di errore
{
	write(STDOUT_FILENO, "\x1b[2J", 4);//\x1b è visto come un solo byte perchè viene interpretato come "27" ovvero l'escape sequence
  	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);//funziona perchè const char *s è l'indirizzo per primo carattere della stringa
	exit(1);
}

void returnCookedMode()
{
	  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
	  	 //dopo di tutto tcsetattr fa TCSAFLUSH, prima scrive &E.orig_termios dentro STDIN_FILENO
	  	error("tcgetattr");
	
}

void rawMode()
{
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)//esegue i comandi nella condizione perchè sennò non potrebbe evaluarli, poi il risultato rimane come
													  //cambiamente "permanente" nel terminale
		error("tcgetattr");

	struct termios raw = E.orig_termios;

	//DISABILITA FLAG PER PERMETTERE EDITING NEL TEXT EDITOR E NON NEL TERMINALE
						
	//faccio un "and" e poi un "not" sui flag delle impostazioni del terminale per settare solo
	//e soltanto i flag che voglio io a off
	
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);//si occupa del terminale

	raw.c_iflag &= ~(ICRNL | IXON);//si occupa dell'input
	raw.c_oflag &= ~(OPOST);//si occupa dell'output

	//*** LISTA FLAG ***//

		//ICANON serve per leggere byte per byte e non stringhe intere, disattivo, quindi, la canonical mode.
		//ISIG disattiva ctrl-c/z/ che equivalgono al terminale il processo corrente, il primo, e sospenderlo il secondo
		//IEXTEN disattiva ctrl-v non pasando in input i caratteri scritti dopo averlo premuto
		//IXON disattiva ctrl-s/q che usiamo per sospendere e riprendere processi
		//ICRNL disattiva ctrl-m che riporta a capo dando in input quello che c'è scritto(carraige return on newline)
		//OPOST si occupa di disattivare la funzione automtica del terminale \r che riporta all'inizio della riga il cursore

	//*** FINE LISTA FLAG ***//

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) 
		error("tcsetattr");
	//flusho cos'ì tutto quello inserito prima della raw mode non verrà letto
											
	atexit(returnCookedMode);//viene chiamata alla fine dell'esecuzione del programma
}

int readKey(void)
{
	int check;
	char c;

	while((check = read(STDIN_FILENO, &c, 1)) != 1)//finchè non gli passo più di un char
	//avendo disattivato ICANON sarebbe anomalo avere più byte passati alla func
	{
		if(check == -1 && errno != EAGAIN)//EAGAIN è un errore non bloccante. Error try Again(?).
			error("read");
	}

	if(c == '\x1b')//escape sequence
	{
		char seq[3];

		if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';//controlliamo che ci sia qualcosa dentro seq
		if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if(seq[0] == '[')//funziona perchè il terminale anche senza specificare perchè STDIN_FILENO legge dentro seq 
		{
			if(seq[1] >= '0' && seq[1] <= '9')//leggiamo le lettere per vedere se dopo ci dovremo mettere la tilde
			{
				if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';

				if(seq[2]=='~')//la tilde precede il comando per pagUp e pagDown
				{
					switch(seq[1])// \x1b[5~ o 6 o 7 ecc arrivati a questo punto
					{
						case '1': return HOME_KEY;
						case '3': return DELETE;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
							
					}
				}
			}
			else
			{
				switch(seq[1])
				{
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}	
		} else if(seq[0] == 'O')
		{
			switch(seq[1])
			{
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
							
			}	
		}
		return '\x1b';
	}
	else
	{
		return c;
	}
}

int cursorPos(int *rows, int *cols)
{
	char buf[80];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)//scriviamo al terminale cosa vogliamo in out
    //n lo usiamo per status info sul terminale la 6° info è quella riguardante la pos
    //del cursore
    	return -1;

    while (i < sizeof(buf) - 1) 
    {
		if (read(STDIN_FILENO, &buf[i], 1) != 1)//scrivo dentro a buf attraverso il suo indirizzo
			break;

        if (buf[i] == 'R') 
        	break;

        i++;
    }

    buf[i] = '\0';

    if (buf[1] == '[')
    	 return -1;

    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) 
    //sscanf() sta per "String Scan Formatted". Questa funzione è usata per analizzare le stringhe formattate 
    //a differenza di scanf() che legge dal flusso di input, la funzione sscanf() estrae i dati da una stringa
    	return -1;

    return 0;

}

int getWindowSize(int *rows, int *cols)//la uso come base per dire alle altre funzioni quanto e dove debbano scrivere la prima volta
{
    struct winsize ws;// viene da <sys/ioctl.h>.
    
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) 
	{						//get win size
    	if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    	//B = cursod down C = Cursor Forward ovvero angolo in basso a destra
    } 
    else 
    {
      *cols = ws.ws_col;
      *rows = ws.ws_row;

  	}
      return 0;
}

/*** Row Operations ***/

int editorRowCxToRx(erow *row, int cx) //char index to render index
{
    int rx = 0;
    int j;

    for (j = 0; j < cx; j++) 
    {
        if (row->chars[j] == '\t')
          rx += (TEIC_TAB - 1) - (rx % TEIC_TAB);

        rx++;
    }
    
    return rx;
}


void editorUpdateRow(erow *row) 
{
    int tabs = 0;
	int idx = 0;
    int j;

    for (j = 0; j < row->size; j++)
      if (row->chars[j] == '\t') tabs++;//number of tabs in a row

    free(row->render);
    row->render = malloc(row->size + tabs*(TEIC_TAB - 1)+ 1);

    for (j = 0; j < row->size; j++)
    {
        if (row->chars[j] == '\t')
        {
            row->render[idx++] = ' ';

            while (idx % TEIC_TAB != 0) row->render[idx++] = ' ';
        } 
        else 
        {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len)//alloco spazio per erow e copio la stringa in un nuovo erow alla fine di E.row
{
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    int at = E.numrows;

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);

    memcpy(E.row[at].chars, s, len);

    E.row[at].chars[len] = '\0';

	E.row[at].rsize = 0;
  	E.row[at].render = NULL;
  	
	editorUpdateRow(&E.row[at]);

    E.numrows++;
}//usa questa funzione che poi verrà chiamata in genTilde() per salvare in un buffer le righe che scriverò

/*** End Row Operations ***/

/*** File I/O ***/

void editorOpen(char *filename)
{
	free(E.filename);
	E.filename = strdup(filename);
	
	FILE *fp = fopen(filename , "r");//perchè così' abbiamo l'indirizzo del file dentro fp
	if(!fp) error("fopen");
	
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;

	//linelen = getline(&line, &linecap, fp);//// DA RIVEDERE
	//legge una riga dalla stream(fp) e salva l'indirizzo del buffer(in &line), il buffer termina in NULL e ha \n alla fine
	//uso &linecap per perchè non essendo largo abbastanza geline userò realloc() e aggiornerà &line e fp
	
	while((linelen = getline(&line, &linecap, fp)) != -1)//finchè non sono a fine file
	{
		while(linelen > 0 && (line[linelen -1] == '\n' || line[linelen -1] == '\r'))
			linelen--;
			//quest loop serve perchè l'ultimo carattere di line dovrebbe essere una \n o \r
	
		editorAppendRow(line, linelen);
	}	

	free(line);//facciamo free perchè abiamo dichiarato *line come NULL e getline richide il free in questo case
	fclose(fp);//flushiamo la stream fp
}

/*** end File I/O ***/

/*** Buffer ***/

void makeBuf(struct tBuf *newBuf, const char *string, int bufLen)//buffer creato per disegnare le tilde
{
	//è char perchè i puntatori sono solo 1 byte
	char *new = realloc(newBuf->buf, newBuf->lenght + bufLen);//newBuf->buf=(*newBuf).buf . new è l'oggetto di grandezza l+bl ma noi puntiamo a quell'oggetto
				//cambio la dimensione del blocco di mememoria puntato da newBuf
	if(new == NULL)
	{	
		return;
	}

	memcpy(&new[newBuf->lenght], string, bufLen);//Questo è un puntatore alla locazione di memoria successiva ai dati dentro "new" correnti
		//= [&new + (*newBuf).lenght]

	//riassegno valori a buf e lenght nella struct per il prossimo ciclo
	newBuf->buf = new;
	newBuf->lenght += bufLen; 
}

void freeBuf(struct tBuf *freeBuff)
{
	free(freeBuff->buf);
}

/*** End Buffer ***/

/*** terminal end ***/

/*** UI start ***/

void editorScroll()
{
	  E.rx = 0;

  	//vertical scroll
    if (E.cy < E.numrows) 
    {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }
     
	if(E.cy >= E.rowoff + E.screenrows) 
	{
		E.rowoff = E.cy - E.screenrows + 1;
	}//sposto verso il basso il cursore a fine schermo

	//horizontal scroll
	if(E.cx < E.coloff)
	{
		E.coloff = E.cx;
	}

	if(E.cx >= E.coloff + E.screencols)
	{
		E.coloff = E.cx - E.screencols + 1;
	}
}

void genTilde(struct tBuf *tildeBuff) //generiamo le tilde da disegnare dopo
{
    int y;
    
    for (y = 0; y < E.screenrows; y++) 
	{
		int filerow = y + E.rowoff;

	    if(filerow >= E.numrows)
	    {
	    	if(E.numrows == 0 && y == 1)
	    	{
	    		char ciao[80];
				int ciaoLen = snprintf(ciao, sizeof(ciao), "TEIC version: %s", TEIC_VERSION);
				//In C, snprintf() function is a standard library function that is used to print the specified string till a specified length in the specified format
				//creo un buffer di caratteri dove metto "TEIC version..."
	
				if(ciaoLen > E.screencols)
				{
					ciaoLen = E.screencols;
				} 
	
				int padding = (E.screencols - ciaoLen)/2;//funziona perchè calcola di stampare  ~ e " " solo per metà schermo
	
				if(padding)
				{
					makeBuf(tildeBuff, "~ ", 1);
					padding--;
				}
		    	while(padding--)
		    	//prendo padding-- perchè si deve aggiornare ancora il risultato nel loop 
		    	//nel mentre disegna lo spazio e poi il titolo
		    	{
		    		makeBuf(tildeBuff, " ", 1);
		    	}
	
				makeBuf(tildeBuff, ciao, ciaoLen);
			}
			else
			{
	    		makeBuf(tildeBuff, "~", 1);
	    	}
		}
		else
		{
			int len = E.row[filerow].rsize - E.coloff;
			//così scrive alla prima riga la prima riga del testo e così via senza strabordare a destra

			if(len < 0) len = 0;
      		if (len > E.screencols) len = E.screencols;//tronto il testo se è troppo lungo

			makeBuf(tildeBuff, &E.row[filerow].render[E.coloff], len);
		}
			makeBuf(tildeBuff, "\x1b[K",3);
			//K lo usiamo per pulire lo schermo una riga alla volta	
			
    	  	makeBuf(tildeBuff, "\r\n", 2);
    	}
}

void editorDrawStatusBar(struct tBuf *buff) 
{
    makeBuf(buff, "\x1b[7m", 4);
	//char status[80];
	char rstatus[80];

	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);

	makeBuf(buff, rstatus, rlen);

    makeBuf(buff, "\x1b[m", 3);
    makeBuf(buff, "\r\n", 2);

   // int len = snprintf(status, sizeof(status), "%.20s - %d lines",
    			      // E.filename ? E.filename : "[No Name]", E.numrows); //errore qui

	
    // if(len > E.screencols)
	// {
		// len = E.screencols;
	// }
	
  //makeBuf(buff, status, len);
 
    // while (len < E.screencols)
    // {
    	// if(E.screencols - len == rlen)
    	// {
			// break;
    	// }
    	// else
    	// {
    		// makeBuf(buff, " ", 1);
    		// len++;
    	// }
 
        // makeBuf(buff, " ", 1);
        // len++;
    // }
}

void editorDrawMessageBar(struct tBuf *buff) 
{
    makeBuf(buff, "\x1b[K", 3);

    int msglen = strlen(E.statusmsg);

    if (msglen > E.scringeencols) msglen = E.screencols;

    if (msglen && time(NULL) - E.statusmsg_time < 5)
      makeBuf(buff, E.statusmsg, msglen);
}

void renderUI()
{
	editorScroll();

	struct tBuf buff = TBUF_INIT;

	makeBuf(&buff, "\x1b[?25l",6);//?25 = nasconde/mostra cursore l = set mode
	//makeBuf(&buff, "\x1b[2J",4);
	makeBuf(&buff, "\x1b[H",3);

	genTilde(&buff);
	editorDrawStatusBar(&buff);
	editorDrawMessageBar(&buff);

	char buf[80];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH",(E.cy - E.rowoff) + 1,
											 (E.cx - E.coloff) + 1);//passiamo a %H c.xe c.y
	makeBuf(&buff, buf, strlen(buf));
	
	makeBuf(&buff, "\x1b[?25h",6);//h = reset mode
	
	write(STDOUT_FILENO,buff.buf, buff.lenght);
	//passiamo 4 byte in memoria:
	//1 byte: \x1b, che equivale all'escale character 27
	//2 byte: [, insieme a \x1b formano una escape sequence, ovvero diciamo al terminale come formattare il testo
	//3+4 byte: 2J, pulisce lo schermo(J) per intero(2)
	//H riposizione il cursore in 1;1 nel temrinale

	freeBuf(&buff);
}

void editorSetStatusMessage(const char *fmt, ...)//funzione variadica, può prendere n parametri in input
{
	va_list ap;

	va_start(ap, fmt);//ftm = ultimo argomento prima di ... 
		vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);

	E.statusmsg_time = time(NULL);
}


/***UI end***/

/*** input start ***/

void moveCursor(int key)
{
	erow *row = (E.cy >= E.numrows) ? NULL: &E.row[E.cy];

	switch(key)
	{
		case ARROW_LEFT:
		if(E.cx != 0)	
		{
			E.cx--;
		}
		else if (E.cy > 0) 
			{
				E.cy--;
				E.cx = E.row[E.cy].size;
			}			
					
		break;
	
		case ARROW_RIGHT:
			if(row && E.cx < row->size)
			{	
				E.cx++;
			}
			else if (row && E.cx == row->size) 
				{
					E.cy++;
					E.cx = 0;
				}			
						
		break;
		
		case ARROW_UP:
			if(E.cy != 0)	
			{
				E.cy--;
			}
		break;
		
		case ARROW_DOWN:
			if(E.cy < E.numrows)
			{
				E.cy++;
			}		
		break;
							
	}

	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;

    if (E.cx > rowlen) 
    {
      E.cx = rowlen;
    }
}

void keypress()
{
	int cPressed = readKey();	

	switch(cPressed)
	{
		case CTRL_KEY('q'):

  			write(STDOUT_FILENO, "\x1b[2J", 4);
  			write(STDOUT_FILENO, "\x1b[H", 3);

			exit(0);//usciamo quando il valore letto è pari al valore di q(01110001) 
									//"ANDato" con 0x1f(0001 1111) che quindi diventa ctrl+q
		break;

		case HOME_KEY:
			E.cx = 0;
			break;


		case END_KEY:
			if(E.cy < E.numrows)
				E.cx = E.row[E.cy].size - 1;
			break;
		
		case PAGE_UP:
		case PAGE_DOWN:
			{
				if (cPressed == PAGE_UP) {
					E.cy = E.rowoff;
        		} 
        		else if (cPressed == PAGE_DOWN) 
        		{
        			  E.cy = E.rowoff + E.screenrows - 1;
        			  if (E.cy > E.numrows) E.cy = E.numrows;
        		}

        		int times = E.screenrows;

				while(times--)
				{
					moveCursor(cPressed == PAGE_UP ? ARROW_UP : ARROW_DOWN);
				}
			}
			break;
		
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			moveCursor(cPressed);
		break;
	
	}
}

/*** input end ***/

/*** initialization ***/

void initEditor() 
{
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;

	E.row = NULL; 
	E.numrows = 0;
	E.rowoff = 0;//è 0 perchè di default siamo in cima al file
	E.coloff = 0;//è 0 perchè di default siamo a sinistra del file

	E.filename = NULL;

	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) error("getWindowSize");

	E.screenrows -= 2;
}


int main(int argc, char *argv[])
{
	rawMode();
	initEditor();

	if(argc >= 2)//perchè ./
	{
		editorOpen(argv[1]);//primo byte del file che fa seguire anche gli altri
	}

	editorSetStatusMessage("HELP: Ctrl-Q = quit");
		
	while (1) 
	{
		renderUI();
    	keypress();
  	}
	//leggiamo da readC 1 byte alla volta
	//dalla stream di input che si crea 
	//automaticamente quando avviamo il programma
	//quando premo invio tutti i caratteri vengono letti 
	//e se c'è una 'q' il programma esce e tutto quello
	//dopo la 'q' viene mandato al terminale.
														
	return 0;
}
