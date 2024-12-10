/*
  Find junctions from the SAM file generated by Tophat2.
  The SAM file should be sorted.
  The junction is the start and the end coordinates of the spliced region in this program. The output of the junction is a bit different.
  
  Usage: ./a.out input [option] >output. 
  	./a.out -h for help.
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "sam.h"

#define LINE_SIZE 8193
#define QUEUE_SIZE 10001
#define HASH_MAX 1000003

struct _readTree
{
	char id[256] ;
	int leftAnchor, rightAnchor ;
	int pos ;
	//bool secondary ;
	bool valid ;
	int editDistance ;
	int NH, cnt ; // if cnt < NH, then it has real secondary match for this splice junction 
	struct _readTree *left, *right ;

	int flag ;// The flag from sam head.
} ;

// The structure of a junction
struct _junction
{
	int start, end ; 
	int readCnt ; // The # of reads containing this junction
	int secReadCnt ; // The # of reads whose secondary alignment has this junction.
	char strand ; // On '+' or '-' strand
	int leftAnchor, rightAnchor ; // The longest left and right anchor
	int oppositeAnchor ; // The longest anchor of the shorter side.
	int uniqEditDistance, secEditDistance ;
	struct _readTree head ;
} ;

// initialising alignment struct 'aln' - contains only the necessary fields for this tool
struct alignment {
    char *qname ;
    char *rname ;
    char *cigar_string ;
    char rnext[2] ;
    char *seq ;
} aln;

char line[LINE_SIZE] ;
char strand ; // Extract XS field
char noncanonStrandInfo ;
//bool secondary ;
int NH ;
int editDistance ;
int mateStart ;
int filterYS ;
int samFlag ;

struct _junction junctionQueue[QUEUE_SIZE] ; // Expected only a few junctions in it for each read. This queue is sorted.

int qHead, qTail ;

bool flagPrintJunction ;
bool flagPrintAll ;
bool flagStrict ; 
int junctionCnt ;
bool anchorBoth ; 
bool validRead ;
int strandedLib ; // 0-unstranded, 1-rf, 2-fr

int flank ;
struct _cigarSeg
{
	int len ;
	char type ;
} ;

struct _readTree *contradictedReads ;

char nucToNum[26] = { 0, 4, 1, 4, 4, 4, 2, 
	4, 4, 4, 4, 4, 4, 4,
		4, 4, 4, 4, 4, 3,
		4, 4, 4, 4, 4, 4 } ;

void PrintHelp()
{
	printf( 
		"Prints reads from the SAM/BAM file that containing junctions.\n" 
		"Usage: ./a.out input [option]>output\n"
		"Options:\n"
	    "\t-j xx [-B]: Output the junctions using 1-based coordinates. The format is \"reference id\" start end \"# of read\" strand.(They are sorted)\n and the xx is an integer means the maximum unqualified anchor length for a splice junction(default=8). If -B, the splice junction must be supported by a read whose both anchors are longer than xx.\n"
	    "\t-a: Output all the junctions, and use non-positive support number to indicate unqualified junctions.\n"
	    "\t-y: If the bits from YS field of bam matches the argument, we filter the alignment (default: 4).\n"
			"\t--stranded un/rf/fr: stranded library fr-firststrand/secondstrand (default: not set).\n"
	      ) ;
}

// frees stack allocated 'alignment' struct fields
void free_alignment(struct alignment *aln) 
{
    if (aln == NULL) return;

    // free the dynamically allocated fields
    if (aln->qname != NULL) {
        free(aln->qname);
        aln->qname = NULL;
    }

    if (aln->rname != NULL) {
        free(aln->rname);
        aln->rname = NULL;
    }

    if (aln->cigar_string != NULL) {
        free(aln->cigar_string);
        aln->cigar_string = NULL;
    }

    if (aln->seq != NULL) {
        free(aln->seq);
        aln->seq = NULL;
    }
}

void GetJunctionInfo( struct _junction &junc, struct _readTree *p )
{
	if ( p == NULL )
		return ;

	if ( p->valid )
	{
		//if ( junc.start == 22381343 + 1 && junc.end == 22904987 - 1 )
		//	printf( "%s %d %d %d\n", p->id, p->leftAnchor, p->rightAnchor, p->flag ) ;
		


		if ( p->cnt < p->NH )
		{
			junc.secEditDistance += p->editDistance ; 	
			++junc.secReadCnt ;
		}
		else
		{
			junc.uniqEditDistance += p->editDistance ;
			++junc.readCnt ;
		}
		int l = p->leftAnchor, r = p->rightAnchor ;

		if ( !anchorBoth )
		{
			if ( l > junc.leftAnchor )
				junc.leftAnchor = l ;
			if ( r > junc.rightAnchor )
				junc.rightAnchor = r ;

			if ( l <= r && l > junc.oppositeAnchor )
				junc.oppositeAnchor = l ;
			else if ( r < l && r > junc.oppositeAnchor )
				junc.oppositeAnchor = r ;
		}
		else
		{
			if ( l > flank && r > flank )
			{
				junc.leftAnchor = l ;
				junc.rightAnchor = r ;
			}

			if ( l <= r && l > junc.oppositeAnchor )
				junc.oppositeAnchor = l ;
			else if ( r < l && r > junc.oppositeAnchor )
				junc.oppositeAnchor = r ;
		}
	}
	GetJunctionInfo( junc, p->left ) ;
	GetJunctionInfo( junc, p->right ) ;
}

void PrintJunctionReads( struct _junction &junc, struct _readTree *p )
{
	if ( p == NULL )
		return ;

	if ( p->valid )
		printf( "%s\n", p->id ) ;
	PrintJunctionReads( junc, p->left ) ;
	PrintJunctionReads( junc, p->right ) ;
}

void PrintJunction( char *chrome, struct _junction &junc )
{
	int sum ;
	junc.leftAnchor = 0 ;
	junc.rightAnchor = 0 ;
	junc.oppositeAnchor = 0 ;
	junc.readCnt = 0 ;
	junc.secReadCnt = 0 ;
	junc.uniqEditDistance = 0 ;
	junc.secEditDistance = 0 ;
	GetJunctionInfo( junc, &junc.head ) ;

	sum = junc.readCnt + junc.secReadCnt ;

	if ( junc.leftAnchor <= flank || junc.rightAnchor <= flank || ( junc.readCnt + junc.secReadCnt <= 0 ) )
	{
		if ( flagPrintAll )
		{
			sum = -sum ;
		}
		else
			return ;
	}
	
	/*if ( junc.end - junc.start + 1 >= 200000 )
	{
		if ( junc.secReadCnt > 0 )
			junc.secReadCnt = 1 ;
	}*/

	/*if ( junc.oppositeAnchor <= ( ( flank / 2 < 1 ) ? flank / 2 : 1 ) && ( junc.readCnt + junc.secReadCnt ) <= 10 )
	{
		if ( junc.readCnt > 0 )
			junc.readCnt = 1 ;
		if ( junc.secReadCnt > 0 )
			junc.secReadCnt = 0 ;
	}*/

	printf( "%s %d %d %d %c %d %d %d %d\n", chrome, junc.start - 1, junc.end + 1, sum, junc.strand, 
		junc.readCnt, junc.secReadCnt, junc.uniqEditDistance, junc.secEditDistance ) ;
	//PrintJunctionReads( junc, &junc.head ) ;
}

void ClearReadTree( struct _readTree *p )
{
	if ( p == NULL )
		return ;
	ClearReadTree( p->left ) ;
	ClearReadTree( p->right ) ;
	free( p ) ;
}

// Insert to the read tree
bool InsertReadTree( struct _readTree *p, char *id, int l, int r )
{
	int tmp = strcmp( p->id, id ) ;
	if ( tmp == 0 )
	{
		p->cnt += 1 ;
		return true ;
	}
	else if ( tmp < 0 )
	{
		if ( p->left )
			return InsertReadTree( p->left, id, l, r ) ;
		else
		{
			p->left = (struct _readTree *)malloc( sizeof( struct _readTree ) ) ;	
			strcpy( p->left->id, id ) ;
			p->left->leftAnchor = l ;
			p->left->rightAnchor = r ;
			//p->left->secondary = secondary ;
			p->left->editDistance = editDistance ;
			p->left->left = p->left->right = NULL ;
			p->left->valid = validRead ;
			p->left->cnt = 1 ;
			p->left->NH = NH ;
			p->left->flag = samFlag ;
			return false ;
		}
	}
	else
	{
		if ( p->right )
			return InsertReadTree( p->right, id, l, r ) ;
		else
		{
			p->right = (struct _readTree *)malloc( sizeof( struct _readTree ) ) ;	
			strcpy( p->right->id, id ) ;
			p->right->leftAnchor = l ;
			p->right->rightAnchor = r ;
			//p->right->secondary = secondary ;
			p->right->editDistance = editDistance ;
			p->right->left = p->right->right = NULL ;
			p->right->valid = validRead ;
			p->right->cnt = 1 ;
			p->right->NH = NH ;
			p->right->flag = samFlag ;
			return false ;
		}
	}
}


bool SearchContradictedReads( struct _readTree *p, char *id, int pos )
{
	if ( p == NULL )
		return false ;
	int tmp = strcmp( p->id, id ) ;
	if ( tmp == 0 && p->pos == pos )
		return true ;
	else if ( tmp <= 0 )
		return SearchContradictedReads( p->left, id, pos ) ;
	else
		return SearchContradictedReads( p->right, id, pos ) ;
}

bool InsertContradictedReads( struct _readTree *p, char *id, int pos )
{
	if ( p == NULL )
	{
		contradictedReads = (struct _readTree *)malloc( sizeof( struct _readTree ) ) ;	
		strcpy( contradictedReads->id, id ) ;
		contradictedReads->pos = pos ;
		contradictedReads->left = contradictedReads->right = NULL ;
		return false ;
	}
	int tmp = strcmp( p->id, id ) ;
	if ( tmp == 0 && p->pos == pos )
		return true ;
	else if ( tmp <= 0 )
	{
		if ( p->left )
			return InsertContradictedReads( p->left, id, pos ) ;
		else
		{
			p->left = (struct _readTree *)malloc( sizeof( struct _readTree ) ) ;	
			strcpy( p->left->id, id ) ;
			p->left->pos = pos ;
			p->left->left = p->left->right = NULL ;
			return false ;
		}
	}
	else
	{
		if ( p->right )
			return InsertContradictedReads( p->right, id, pos ) ;
		else
		{
			p->right = (struct _readTree *)malloc( sizeof( struct _readTree ) ) ;	
			strcpy( p->right->id, id ) ;
			p->right->pos = pos ;
			p->right->left = p->right->right = NULL ;
			return false ;
		}
	}
}

struct _readTree *GetReadTreeNode( struct _readTree *p, char *id )
{
	if ( p == NULL )
		return NULL ;
	int tmp = strcmp( p->id, id ) ;
	if ( tmp == 0 )
		return p ;
	else if ( tmp < 0 )
		return GetReadTreeNode( p->left, id ) ;
	else
		return GetReadTreeNode( p->right, id ) ;
}

// Insert the new junction into the queue, 
// and make sure the queue is sorted.
// Assume each junction range is only on one strand. 
// l, r is the left and right anchor from a read
void InsertQueue( int start, int end, int l, int r )
{
	int i, j ;
	i = qTail ;
	

	while ( i != qHead )
	{
		j = i - 1 ;
		if ( j < 0 )
			j = QUEUE_SIZE - 1 ;
		
		if ( junctionQueue[j].start < start 
			|| ( junctionQueue[j].start == start && junctionQueue[j].end < end ) )
			break ;
		
		junctionQueue[i] = junctionQueue[j] ;
		--i ;
		
		if ( i < 0 )
			i = QUEUE_SIZE - 1 ;
	}
	
	junctionQueue[i].start = start ;
	junctionQueue[i].end = end ;
	/*if ( !secondary )
	{
		junctionQueue[i].readCnt = 1 ;
		junctionQueue[i].secReadCnt = 0 ;
	}
	else
	{
		junctionQueue[i].readCnt = 0 ;
		junctionQueue[i].secReadCnt = 1 ;
	}*/
	junctionQueue[i].strand = strand ;
	junctionQueue[i].leftAnchor = l ;
	junctionQueue[i].rightAnchor = r ;
	
	strcpy( junctionQueue[i].head.id, aln.qname ) ;
	junctionQueue[i].head.valid = validRead ;
	junctionQueue[i].head.leftAnchor = l ;
	junctionQueue[i].head.rightAnchor = r ;
	junctionQueue[i].head.left = junctionQueue[i].head.right = NULL ;
	//junctionQueue[i].head.secondary = secondary ;
	junctionQueue[i].head.NH = NH ;
	junctionQueue[i].head.cnt = 1 ;
	junctionQueue[i].head.editDistance = editDistance ;

	++qTail ;
	if ( qTail >= QUEUE_SIZE )
		qTail = 0 ;
}

// Search the queue, and remove the heads if its end(start) is smaller than prune.(Because it is impossible to have that junction again) 
// Add the junction to the queue, if it is not in it. 
// Return true, if it finds the junction. Otherwise, return false.
bool SearchQueue( int start, int end, int prune, int l, int r )
{
	int i ;
	
	// Test whether this read might be a false alignment.
	i = qHead ;
	while ( i != qTail )
	{
		struct _readTree *rt ;
		if ( ( junctionQueue[i].start == start && junctionQueue[i].end < end ) ||
			( junctionQueue[i].start > start && junctionQueue[i].end == end ) )
		{
			// This alignment is false ;
			rt = GetReadTreeNode( &junctionQueue[i].head, aln.qname ) ;
			// the commented out logic because it is handled by contradicted reads
			if ( rt != NULL ) //&& ( rt->flag & 0x40 ) != ( samFlag & 0x40 ) )
			{
				if ( rt->leftAnchor <= flank || rt->rightAnchor <= flank )//|| rt->secondary )
				{
					if ( l > flank && r > flank )//&& !secondary ) 
						rt->valid = false ;
					else
						validRead = false ;
				}
				else
				{
					// Ignore this read
					//return true ;
					validRead = false ;
				}
			}
		}
		else if ( ( junctionQueue[i].start == start && junctionQueue[i].end > end ) ||
			( junctionQueue[i].start < start && junctionQueue[i].end == end ) )
		{
			// This other alignment is false ;
			rt = GetReadTreeNode( &junctionQueue[i].head, aln.qname ) ;
			//if ( rt != NULL )
			if ( rt != NULL ) //&& ( rt->flag & 0x40 ) != ( samFlag & 0x40 ) )
			{
				if ( l <= flank || r <= flank )//|| secondary )
				{
					if ( rt->leftAnchor > flank && rt->rightAnchor > flank )//&& !rt->secondary )
						validRead = false ;
					else
						rt->valid = false ;
				}
				else	
					rt->valid = false ;
			}
		}
		++i ;
		if ( i >= QUEUE_SIZE )
			i = 0 ;
	}
	
	//if ( start == 8077108 && end == 8078439 )
	//	exit( 1 ) ;
	i = qHead ;

	while ( i != qTail )
	{
		if ( junctionQueue[i].start == start && 
			junctionQueue[i].end == end )
		{
			if (junctionQueue[i].strand == '?' && junctionQueue[i].strand != strand )
			{
				junctionQueue[i].strand = strand ;
			}
			InsertReadTree( &junctionQueue[i].head, aln.qname, l, r ) ; 
			return true ;
		}
		
		if ( junctionQueue[i].end < prune && i == qHead )
		{
			// pop
			PrintJunction( aln.rname, junctionQueue[i] ) ;
			ClearReadTree( junctionQueue[i].head.left ) ;
			ClearReadTree( junctionQueue[i].head.right ) ;
			++qHead ;
			if ( qHead >= QUEUE_SIZE )
				qHead = 0 ;
		}
		++i ;
		if ( i >= QUEUE_SIZE )
			i = 0 ;
	}
	
	InsertQueue( start, end, l, r ) ;
	return false ;
}

// Compute the junctions based on the CIGAR  
bool CompareJunctions( int startLocation, char *cigar )
{
	int currentLocation = startLocation ; // Current location on the reference genome
	int i, j ;
	int num ;
	int newJuncCnt = 0 ; // The # of junctions in the read, and the # of new junctions among them.
	
	struct _cigarSeg cigarSeg[2000] ; // A segment of the cigar.
	int ccnt = 0 ; // cigarSeg cnt

	j = 0 ;
	num = 0 ;
	validRead = true ;

	for ( i = 0 ; cigar[i] ; ++i )
	{
		if ( cigar[i] >= '0' && cigar[i] <= '9' )
		{
			num = num * 10 + cigar[i] - '0' ;
		}
		else
		{
			cigarSeg[ccnt].len = num ;
			cigarSeg[ccnt].type = cigar[i] ;
			++ccnt ;
			num = 0 ;
		}
	}

	// Filter low complex alignment.
	// Only applies this to alignments does not have a strand information.
	if ( strand == '?' )
	{
		if ( noncanonStrandInfo != -1 )
		{
			if ( ( noncanonStrandInfo & filterYS ) != 0 )
				validRead = false ;		
		}
		else
		{
			/*
			 * REMOVED: Unused variables
			int softStart = -1 ;
			int softEnd = 0 ;
			if ( cigarSeg[0].type == 'S' )
				softStart = cigarSeg[0].len ;
			if ( cigarSeg[ ccnt - 1 ].type == 'S' )
				softEnd = cigarSeg[ ccnt - 1 ].len ;
			int readLen = strlen( col[9] ) ; // will need to change col[9] to aln.seq
			*/
			int count[5] = { 0, 0, 0, 0, 0 } ;

			int pos = 0 ;
			for ( i = 0 ; i < ccnt ; ++i )
			{
				switch ( cigarSeg[i].type )
				{
					case 'S':
						pos += cigarSeg[i].len ;
					case 'M':
					case 'I':
						{
							for ( j = 0 ; j < cigarSeg[i].len ; ++j )
								++count[ (unsigned char) nucToNum[  aln.seq[pos + j] - 'A' ] ] ;
							pos += j ;
						} break ;
					case 'N':
						{
							int max = 0 ;
							int sum = 0 ;
							for ( j = 0 ; j < 5 ; ++j )
							{
								if ( count[j] > max )
									max = count[j] ;
								sum += count[j] ;
							}
							if ( max > 0.8 * sum ) {
								validRead = false ;
							}
							count[0] = count[1] = count[2] = count[3] = count[4] = 0 ;
						} break ;
					case 'H':
					case 'P':
					case 'D':
					default: break ;
				}
			}

			int max = 0 ;
			int sum = 0 ;
			for ( j = 0 ; j < 5 ; ++j )
			{
				if ( count[j] > max ) {
					max = count[j] ;
				}
				sum += count[j] ;
			}
			if ( max > 0.8 * sum )
				validRead = false ;
			/*	count[0] = count[1] = count[2] = count[3] = count[4] = 0 ;


			for ( i = softStart + 1 ; i < readLen - softEnd ; ++i )
			  {
			  switch ( col[9][i] ) // will need to change to aln.seq
			  {
			  case 'A': ++count[0] ; break ;
			  case 'C': ++count[1] ; break ;
			  case 'G': ++count[2] ; break ;
			  case 'T': ++count[3] ; break ;
			  default: ++count[4] ; 
			  }
			  }
			  int max = 0 ;
			  for ( j = 0 ; j < 5 ; ++j )
			  if ( count[j] > max )
			  max = count[j] ;
			  if ( max > 0.6 * ( readLen - softEnd - softStart - 1 ) )
			  validRead = false ;*/
		}
	}

	// Test whether contradict with mate pair
	if ( aln.rnext[0] == '=' )
	{
		currentLocation = startLocation ;
		for ( i = 0 ; i < ccnt ; ++i )
		{
			if ( cigarSeg[i].type == 'I' || cigarSeg[i].type == 'S' || cigarSeg[i].type == 'H'
				|| cigarSeg[i].type == 'P' )
				continue ;
			else if ( cigarSeg[i].type == 'N' )
			{
				if ( mateStart >= currentLocation && mateStart <= currentLocation + cigarSeg[i].len - 1 )
				{
				/*if ( cigarSeg[i].len == 91486 )
				{
					printf( "%s %d %d\n", currentLocation, mateStart ) ;
					exit( 1 ) ;
				}*/
					// ignore this read
					//return false ;
					InsertContradictedReads( contradictedReads, aln.qname, mateStart ) ;
					validRead = false ;
					break ;
				}
			}
			currentLocation += cigarSeg[i].len ;
		}

		i = qHead ;
		// Search if it falls in the splices junction created by its mate.
		while ( i != qTail )
		{
			if ( mateStart < junctionQueue[i].start && junctionQueue[i].start <= startLocation 
				&& startLocation <= junctionQueue[i].end 
				&& SearchContradictedReads( contradictedReads, aln.qname, startLocation ) )
			{
				validRead = false ;
				break ;
			}
			++i ;
			if ( i >= QUEUE_SIZE )
				i = 0 ;
		}
	}

	currentLocation = startLocation ;
	for ( i = 0 ; i < ccnt ; ++i )
	{
		if ( cigarSeg[i].type == 'I' || cigarSeg[i].type == 'S' || cigarSeg[i].type == 'H'
				|| cigarSeg[i].type == 'P' )
			continue ;
		else if ( cigarSeg[i].type == 'N' )
		{
			int left, right ;
			int tmp ;
			tmp = i ;
			while ( i > 0 && ( cigarSeg[i - 1].type == 'I' || cigarSeg[i - 1].type == 'D' ) )
				--i ;
			if ( i > 0 && cigarSeg[i - 1].type == 'M' )
			{
				left = cigarSeg[i - 1].len ;
				if ( i >= 2 && cigarSeg[i - 2].type == 'N' && left <= flank )
				{
					left = flank + 1 ;
				}
			}
			else
				left = 0 ;

			i = tmp ;
			while ( i < ccnt && ( cigarSeg[i + 1].type == 'I' || cigarSeg[i + 1].type == 'D' ) )
				++i ;
			if ( i < ccnt && cigarSeg[i + 1].type == 'M' )
			{
				right = cigarSeg[i + 1].len ;
				if ( i + 2 < ccnt && cigarSeg[i + 2].type == 'N' && right <= flank )
				{
					right = flank + 1 ;
				}
			}
			else
				right = 0 ;
			i = tmp ;
			if ( !SearchQueue( currentLocation, currentLocation + cigarSeg[i].len - 1, startLocation, left, right ) )
			{
				++newJuncCnt ;
			}
		}
		currentLocation += cigarSeg[i].len ;
	}
		/*else if ( cigar[i] == 'I' )
		{
			num = 0 ;
		}
		else if ( cigar[i] == 'N' )
		{
			if ( !SearchQueue( currentLocation, currentLocation + num - 1, startLocation ) )
			{
				++newJuncCnt ;
				//if ( flagPrintJunction )
                // NOTE: col replaced with alignment struct 
				//	printf( "%s %d %d\n", col[2], currentLocation - 2, currentLocation + len - 1 ) ;
			}
			currentLocation += num ;
			num = 0 ;
		}
		else // Other operations, like M, D,...,
		{
			currentLocation += num ;
			num = 0 ;
		}*/
	
	junctionCnt += newJuncCnt ;
	
	if ( newJuncCnt )
		return true ;
	else
		return false ;
}

void cigar2string( bam1_core_t *c, uint32_t *in_cigar, char **out_cigar )
{
	int k, op, l ;
	char opcode ;
    int string_length = 0;

    for (k = 0 ; k < c->n_cigar ; ++k )
    {
        l = in_cigar[k] >> BAM_CIGAR_SHIFT ;
        string_length += (l == 0) ? 1 : (int)log10(abs(l)) + 1;
        string_length++;
    }

    string_length++;

    *out_cigar = (char *) malloc(string_length + 1);

    if (*out_cigar == NULL) {
        printf("Out of memory!\n");
        exit(EXIT_FAILURE);
    }

    *(*out_cigar) = '\0';
	for ( k = 0 ; k < c->n_cigar ; ++k )
	{
		op = in_cigar[k] & BAM_CIGAR_MASK ;
		l = in_cigar[k] >> BAM_CIGAR_SHIFT ;
		switch (op)
		{
			case BAM_CMATCH: opcode = 'M' ; break ;
			case BAM_CINS: opcode = 'I' ; break ;
			case BAM_CDEL: opcode = 'D' ; break ;
			case BAM_CREF_SKIP: opcode = 'N' ; break ;
			case BAM_CSOFT_CLIP: opcode = 'S' ; break ;
			case BAM_CHARD_CLIP: opcode = 'H' ; break ;
			case BAM_CPAD: opcode = 'P' ; break ;
		}
		sprintf( *out_cigar + strlen( *out_cigar ), "%d%c", l, opcode ) ;
	}
}

char GetStrandFromStrandedLib(int flag)
{
	if (strandedLib == 0)
		return '?' ;
	if ((flag & 0x80) == 0) // first read or single-end case
	{
		return  (((flag >> 4) & 1) ^ (strandedLib & 1)) ? '-' : '+' ;
	}
	else
	{
		return  (((flag >> 4) & 1) ^ (strandedLib & 1)) ? '+' : '-' ;
	}
}

int main( int argc, char *argv[] ) 
{
 	samfile_t *fpsam ;
	bam1_t *b = NULL ;

 	int i, len ;
 	int startLocation ; 
	bool flagRemove = false ;
	char prevChrome[103] ;

	anchorBoth = false ;	
	
	flagPrintJunction = false ;
	flagPrintAll = false ;
	flagStrict = false ;
	junctionCnt = 0 ;
	bool hasMateReadIdSuffix = false ;

	strcpy( prevChrome, "" ) ;
	flagRemove = true ;
	flagPrintJunction = true ;
	flank = 8 ;
	filterYS = 4 ;
	strandedLib = 0 ;

	contradictedReads = NULL ;
	
	// processing the argument list
	for ( i = 1 ; i < argc ; ++i )
	{
		if ( !strcmp( argv[i], "-h" ) )
		{
			PrintHelp() ;
			return 0 ;
		}
		else if ( !strcmp( argv[i], "-r" ) )
		{
			flagRemove = true ;
		}
		else if ( !strcmp( argv[i], "-j" ) )
		{
			strcpy( prevChrome, "" ) ;
			flagRemove = true ;
			flagPrintJunction = true ;
			flank = atoi( argv[i + 1] ) ;
			if ( i + 2 < argc && !strcmp( argv[i+2], "-B" ) )
			{
				anchorBoth = true ;
				++i ;		
			}
			++i ;
		}
		else if ( !strcmp( argv[i], "-a" ) )
		{
			flagPrintAll = true ;
		}
		else if ( !strcmp( argv[i], "--strict" ) )
		{
			flagStrict = true ;
		}
		else if ( !strcmp( argv[i], "-y" ) )
		{
			filterYS = atoi( argv[i + 1] ) ;
			++i ;
		}
		else if ( !strcmp( argv[i], "--hasMateIdSuffix" ) )
		{
			hasMateReadIdSuffix = true ;
			++i ;
		}
		else if ( !strcmp( argv[i], "--stranded" ) )
		{
			if ( !strcmp(argv[i + 1], "un") ) 
				strandedLib = 0 ;
			else if ( !strcmp(argv[i + 1], "rf") )
				strandedLib = 1 ;
			else if ( !strcmp(argv[i + 1], "fr" ) )
				strandedLib = 2 ;
			++i ;
		}
		else if ( i > 1 )
		{
			printf( "Unknown option %s\n", argv[i] ) ;
			exit( 1 ) ;
		}
	}
	if ( argc == 1 )
	{
		PrintHelp() ;
		return 0 ;
	}

	len = strlen( argv[1] ) ;
	if ( argv[1][len-3] == 'b' || argv[1][len-3] == 'B' ) // is file .bam?
	{	
		if ( !( fpsam = samopen( argv[1], "rb", 0 ) ) )
			return 0 ;

		if ( !fpsam->header )
		{
            printf( "Could not open file %s\n (not a valid bam file)\n", argv[1] ) ;
            exit(EXIT_FAILURE);
		}
	}
	else // assume .sam
	{
		if ( !( fpsam = samopen( argv[1], "r", 0 ) ) )
			return 0 ;

		if ( !fpsam->header )
		{
            printf( "Could not open file %s\n (not a valid sam file)\n", argv[1] ) ;
            exit(EXIT_FAILURE);
		}
	}

	while ( 1 )
	{
		int flag = 0 ;
        if ( b )
            bam_destroy1( b ) ;
        b = bam_init1() ;
        if ( samread( fpsam, b ) <= 0 )
            break ;
        if ( b->core.tid >= 0 )
            aln.rname = strdup( fpsam->header->target_name[b->core.tid] ) ;
        else
            continue ;
            //strcpy( col[2], "-1" ) ;

        cigar2string( &(b->core), bam1_cigar( b ), &aln.cigar_string ) ;

        // Dynamically allocating and assigning query name
        aln.qname = strdup(bam1_qname( b )) ;

        if (aln.qname == NULL) {
            printf("Out of memory!\n") ;
            exit(EXIT_FAILURE) ;
        }

        flag = b->core.flag ;	
        if ( bam_aux_get( b, "NH" ) )
        {	
            /*if ( bam_aux2i( bam_aux_get(b, "NH" ) ) >= 2 )
            {
                secondary = true ;
            }
            else
                secondary = false ;*/

            NH = bam_aux2i( bam_aux_get( b, "NH" ) ) ;
        }
        else
        {
            //secondary = false ;
            NH = 1 ;
        }

        if ( bam_aux_get( b, "NM" ) )
        {
            editDistance = bam_aux2i( bam_aux_get( b, "NM" ) ) ;
        }
        else if ( bam_aux_get( b, "nM" ) )
        {
            editDistance = bam_aux2i( bam_aux_get( b, "nM" ) ) ;
        }
        else
            editDistance = 0 ;

        mateStart = b->core.mpos + 1 ;
        if ( b->core.mtid == b->core.tid )
            aln.rnext[0] = '=' ;
        else
            aln.rnext[0] = '*' ;		

        if ( b->core.l_qseq < 20 )
            continue ;
        
        aln.seq = (char *) malloc(b->core.l_qseq + 1);
        for ( i = 0 ; i < b->core.l_qseq ; ++i )
        {
            int bit = bam1_seqi( bam1_seq( b ), i ) ;
            switch ( bit )
            {
                case 1: aln.seq[i] = 'A' ; break ;
                case 2: aln.seq[i] = 'C' ; break ;
                case 4: aln.seq[i] = 'G' ; break ;
                case 8: aln.seq[i] = 'T' ; break ;
                case 15: aln.seq[i] = 'N' ; break ;
                default: aln.seq[i] = 'A' ; break ;
            }	
        }
        aln.seq[i] = '\0' ;

        /*if ( flag & 0x100 )
            secondary = true ;
        else 
            secondary = false ;*/

		samFlag = flag ;
		for ( i = 0 ; aln.cigar_string[i] ; ++i )
			if ( aln.cigar_string[i] == 'N' )
				break ;

		if ( !aln.cigar_string[i] ) {
            free_alignment(&aln) ;
			continue ;
        }
		
		// remove .1, .2 or /1, /2 suffix
		if ( hasMateReadIdSuffix )
		{
			char *s = aln.qname ;
			int len = strlen( s ) ;
			if ( len >= 2 && ( s[len - 1] == '1' || s[len - 1] == '2' ) 
				&& ( s[len - 2] == '.' || s[len - 2] == '/' ) )
			{
				s[len - 2] = '\0' ;
			}
		}

        if ( bam_aux_get( b, "XS" ) )
        {
            strand = bam_aux2A( bam_aux_get( b, "XS" ) ) ;
            if ( bam_aux_get( b, "YS" ) )
            {
                noncanonStrandInfo = bam_aux2i( bam_aux_get( b, "YS" ) ) ;
            }
            else
            {
                noncanonStrandInfo = -1 ;
            }
        }
        else if ( strandedLib != 0 ) 
        {
            strand = GetStrandFromStrandedLib(flag) ;
            noncanonStrandInfo = -1 ;
        }
        else
        {
            strand = '?' ;
            noncanonStrandInfo = -1 ;
        }
        startLocation = b->core.pos + 1 ;
		
		// Found the junctions from the read.
		if ( strcmp( prevChrome, aln.rname ) )
		{
			if ( flagPrintJunction )
			{
				// Print the remaining elements in the queue
				i = qHead ;
				while ( i != qTail )
				{
					PrintJunction( prevChrome, junctionQueue[i] ) ;
					++i ;
					if ( i >= QUEUE_SIZE )
						i = 0 ;
				}
			}

			// new chromosome
			ClearReadTree( contradictedReads ) ;
			contradictedReads = NULL ;
			qHead = qTail = 0 ;
			strcpy( prevChrome, aln.rname ) ;
		}

		if ( flagRemove )
		{
			if ( CompareJunctions( startLocation, aln.cigar_string ) )
			{
				// Test whether this read has new junctions
				//++junctionCnt ;
				if ( !flagPrintJunction )
					printf( "%s", line ) ;
			}
		}
		else
		{
			++junctionCnt ;
			printf( "%s", line ) ;
		}
		//printf( "hi2 %s\n", col[0] ) ;

        free_alignment(&aln);
	}
	
	if ( flagPrintJunction )
	{
		// Print the remaining elements in the queue
		i = qHead ;
		while ( i != qTail )
		{
			PrintJunction( prevChrome, junctionQueue[i] ) ;
			++i ;
			if ( i >= QUEUE_SIZE )
				i = 0 ;
		}
	}	
	
	//fprintf( stderr, "The number of junctions: %d\n", junctionCnt ) ;
    samclose(fpsam);
    bam_destroy1(b);
	return 0 ;
}

