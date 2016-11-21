#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define MAX_CANDS 10000
#define MAX_GROUPS 1000
#define MAX_MEMBERS 100

typedef struct  {
	int n;
	double value;
	int * ids;
} Vote;

typedef struct {
	char * name;
	int n;
	int * members;
} Group;

typedef struct {
	char * name;
	size_t nref, refcap;
	int group, in_race;
	int * refs;
} Candidate;

int parse_cands(FILE * file, char * area, Candidate ** cands, Group ** groups, int * ngroup)
{
	int i, j, ci, in_quote, in_area, senate, nlen, gind, ncand;
	char * line = NULL, *token, *tokarg, *s1, *s2, c;
	char * tokens[10], *gname, *prevname = "";
	Candidate * cand;
	Group * group;
	size_t len = 0;
	ssize_t read = 0;
	*groups = malloc(MAX_GROUPS*sizeof(Group));
	*ngroup = 0;
	*cands  = malloc(MAX_CANDS*sizeof(Candidate));
	ncand   = 0;

	// The first line is a comment
	read = getline(&line, &len, file);
	// Then loop through and parse lines. This doesn't need to be very fast
	for(i=0;(read = getline(&line, &len, file))!=-1;i++) {
		// Example format:
		// "2016 Federal Election","S","WA","","AB",2,"HENG","Henry","Family First Party","Chairman and CEO","","","","","","","","","","","","","","0400 519 023","henryheng@outlook.com"
		// Interesting fields are $2, which should be S,
		// $3, which should be our area, $5, which is the group name, and
		// $6, which is the group index (from 1). $7 and $8 are the names.
		// Loop over tokens while tolerating , inside quotes. We don't need
		// the last fields, so skip them. This means we can also be naughty and
		// skip the EOL check.
		for(s1=line,j=0;j<10;s1=s2+1,j++) {
			// Find comma that ends this token
			in_quote = 0;
			for(s2=s1;;s2++) {
				if(*s2 == ',' && !in_quote) break;
				in_quote ^= *s2 == '"';
			}
			*s2 = 0;
			// Get rid of quotes
			if(*s1 == '"') { s1++; *(s2-1) = 0; }
			// Save our token
			tokens[j] = s1;
		}
		if(tokens[1][0] != 'S') continue;
		if(strcmp(tokens[2],area)) continue;

		// Ok, this candidate and group are ok.
		// First set up the group
		gname = tokens[4];
		if(*ngroup == 0 || strcmp((*groups)[*ngroup-1].name, gname)) {
			// New group
			(*ngroup)++;
			group = &(*groups)[*ngroup-1];
			group->name = strdup(gname);
			group->n    = 0;
			group->members = malloc(MAX_MEMBERS*sizeof(int));
			//printf("New group %2d %s\n", *ngroup-1, group->name);
		}
		// Then set up the candidate
		ncand++;
		cand = &(*cands)[ncand-1];
		// Wow, that's a complicated way of writing cand->name = tokens[7] + " " + tokens[6]
		nlen = strlen(tokens[6])+strlen(tokens[7])+1+1;
		cand->name = malloc(nlen*sizeof(char));
		strcpy(cand->name, tokens[7]);
		strcat(cand->name, " ");
		strcat(cand->name, tokens[6]);
		// Group membership
		cand->group = *ngroup-1;
		group->members[group->n++] = ncand-1;
		//printf("New cand %3d %s\n", ncand-1, cand->name);
	}
	return ncand;
}

int parse_votes(FILE * file, Vote ** votes, Candidate * cands, int ncand, Group *groups, int ngroup, int strict)
{
	int i, j, k, l, vi, gi, rank, capacity = 16, invalid;
	int ranks[MAX_CANDS], ranks_both[MAX_CANDS], nabove, nbelow, max_rank;
	char * line = NULL, *token, *tokarg, *s1, *s2, c;
	size_t len = 0;
	ssize_t read = 0;
	*votes = malloc(capacity*sizeof(Vote));
	// The first two lines are comments. Skip them
	for(i=0;i<2;i++) read = getline(&line, &len, file);
	// Get the first real line and determine the number of candidates
	for(i=0,vi=0;(read = getline(&line, &len, file))!=-1;i++) {
		//printf("line: %s", line);
		if(vi+1>=capacity) {
			capacity *= 2;
			*votes = realloc(*votes, capacity*sizeof(Vote));
		}
		Vote * vote = &(*votes)[vi];
		vote->n = 0;
		vote->value = 1.0;
		invalid = 0;
		max_rank = 0;
		// Manual tokenization was much faster than strsep
		for(s2=line;*s2!='"';s2++);
		for(j=-1;;) {
			// Seek to first valid entry
			for(s1=s2+1,j++;*s1==',';s1++,j++);
			if(*s1=='"') break; // no valid entry
			// Seek to end of entry
			for(s2=s1+1; *s2!=','&&*s2!='"'; s2++);
			// Parse number
			c = *s2;
			*s2 = 0;
			rank = atoi(s1);
			if(rank < 1) { invalid = 1; break; }
			ranks_both[rank-1] = j;
			vote->n++;
			if(rank > max_rank) max_rank = rank;
			if(c=='"') break;
		}
		if(invalid) continue;
		// Check if ranks have been duplicated or skipped
		if(vote->n != max_rank) continue;
		// Count number of above and below the line votes
		nabove = nbelow = 0;
		for(j=0;j<vote->n;j++)
			if(ranks_both[j]<ngroup) nabove++;
			else nbelow++;
		// To be strict, we need at least a certain number of votes
		// either above or below the line
		if(strict) {
			if(!(nabove >= 6 || nbelow >= 12)) continue;
			if(nabove && nbelow) continue;
		}
		//printf("ngroup: %d\n", ngroup);
		//printf("Raw ranks:");
		//for(j=0;j<vote->n;j++) printf(" %d",ranks_both[j]);
		//printf("\n");
		// Exand groups, so that we have a plain, candidate-only ranking
		for(j=0,k=0;j<vote->n;j++) {
			if(ranks_both[j] < ngroup) {
				gi = ranks_both[j];
				for(l=0;l<groups[gi].n;l++)
					ranks[k++] = groups[gi].members[l];
			} else {
				ranks[k++] = ranks_both[j] - ngroup;
			}
		}
		vote->n = k;
		//printf("Remapped:");
		//for(j=0;j<vote->n;j++) printf(" %d",ranks[j]);
		//printf("\n");
		// Ok, after this point we don't need to think about groups any more
		vote->ids = malloc(vote->n*sizeof(int));
		memcpy(vote->ids, ranks, vote->n*sizeof(int));
		vi++;
	}
	return vi;
}

void init_candidate_refs(Vote * votes, int nvote, Candidate * cands, int ncand)
{
	int i, j, id;
	for(id=0; id<ncand; id++) {
		cands[id].in_race = 1;
		cands[id].nref = 0;
		cands[id].refcap = 16;
		cands[id].refs = malloc(cands[id].refcap*sizeof(int));
	}
	for(i=0; i<nvote; i++) {
		for(j=0; j<votes[i].n; j++) {
			id = votes[i].ids[j];
			if(cands[id].nref+1 >= cands[id].refcap) {
				cands[id].refcap *= 2;
				cands[id].refs = realloc(cands[id].refs, cands[id].refcap*sizeof(int));
			}
			cands[id].refs[cands[id].nref++] = i;
		}
	}
}

void free_candidates(Candidate * cands, int ncand) {
	int i;
	for(i=0;i<ncand;i++) free(cands[i].refs);
	free(cands);
}

void count_votes(Vote * votes, int nvote, double * counts, int ncand)
{
	int i, j;
	for(i=0; i<ncand; i++)
		counts[i] = 0;
	for(i=0; i<nvote; i++)
		if(votes[i].n)
			counts[votes[i].ids[0]] += votes[i].value;
}

void eliminate_candidate(Vote * votes, Candidate * cands, int id, double * counts, double scaling)
{
	int ri, vi, a, b, was_first;
	cands[id].in_race = 0;
	for(ri=0;ri<cands[id].nref;ri++) {
		vi = cands[id].refs[ri];
		// Is the elminated candidate our first remaining choice? If so,
		// part of our vote may have been used up, so rescale.
		was_first = votes[vi].ids[0] == id;
		if(was_first)
			votes[vi].value *= scaling;
		// Eliminate canidate from any location in our ranked list
		for(a=0,b=0;b<votes[vi].n;b++)
			if(votes[vi].ids[b] != id)
				votes[vi].ids[a++] = votes[vi].ids[b];
		votes[vi].n = a;
		// Update vote count for new first choice
		if(was_first && votes[vi].n > 0)
			counts[votes[vi].ids[0]] += votes[vi].value;
	}
	// And remove all votes for current candidate
	counts[id] = 0;
}

int stv_round(Vote * votes, int nvote, int ncand, int * elected, int nelect, int ndone, Candidate * cands, double * counts)
{
	int i, imin, imax, nleft = 0;
	int quota = nvote/(nelect+1)+1;
	double vote_scaling, minvotes, maxvotes;
	//printf("nvote: %d nelect: %d quota: %d ncand: %d\n", nvote, nelect, quota, ncand);
	// How many candidates are left running? All remaining ones
	// win if we have enough seats for them left.
	for(i=0;i<ncand;i++) nleft += cands[i].in_race > 0;
	//printf("nleft: %d\n", nleft);
	if(nleft <= nelect - ndone) {
		for(i=0;i<ncand;i++)
			if(cands[i].in_race) {
				elected[ndone++] = i;
				cands[i].in_race = 0;
			}
		while(ndone<nelect) elected[ndone++] = -1;
		return ndone;
	}
	// Ok, not everybody could win. But there could still be a winner.
	//count_votes(votes, nvote, counts2, ncand);
	//for(i=0;i<ncand;i++)
	//	if(cands[i].in_race) printf("%3d %15.3f\n", i, counts[i]);
	// Find highest-count candidate
	for(i=0,imax=0;i<ncand;i++)
		if(cands[i].in_race && counts[i] > counts[imax])
			imax = i;
	if(counts[imax] >= quota) {
		elected[ndone++] = imax;
		vote_scaling = 1-quota/counts[imax];
		//printf("Electing %d with %15.3f scaling %8.5f (%s)\n",imax, counts[imax], vote_scaling, cands[imax].name);
		eliminate_candidate(votes, cands, imax, counts, vote_scaling);
		return ndone;
	}
	// No winner, so elminate a loser.
	minvotes = 1.0/0.0;
	for(i=0,imin=0;i<ncand;i++)
		if(cands[i].in_race && counts[i] < minvotes) {
			minvotes = counts[i];
			imin = i;
		}
	//printf("Eliminating %d (%s)\n",imin, cands[imin].name);
	eliminate_candidate(votes, cands, imin, counts, 1.0);
	return ndone;
}

void run_stv(Vote * votes, int nvote, Candidate * cands, int ncand, int * elected, int nelect)
{
	int i, round, ndone = 0;
	init_candidate_refs(votes, nvote, cands, ncand);
	double * counts = malloc(ncand*sizeof(double));
	// Get initial vote counts
	count_votes(votes, nvote, counts, ncand);
	for(round=0;ndone < nelect;round++)
	{
		ndone = stv_round(votes, nvote, ncand, elected, nelect, ndone, cands, counts);
		//printf("round %3d:",round);
		//for(i=0;i<ndone;i++) printf(" %d",elected[i]);
		//printf("\n");
	}
	free(counts);
}

int main(int argc, char ** argv)
{
	int nelect, i, nvote, ncand, ngroup;
	int strict = 0;
	int * elected;
	FILE * file;
	char * cand_fname, * vote_fname, * area;
	Candidate * cands;
	Group * groups;
	Vote * votes;
	if(argc != 5) {
		fprintf(stderr, "Usage: stv cand_file vote_file area num_to_elect\n");
		return 1;
	}
	cand_fname = argv[1];
	vote_fname = argv[2];
	area       = argv[3];
	nelect     = atoi(argv[4]);

	ncand = parse_cands(fopen(cand_fname,"r"), area, &cands, &groups, &ngroup);
	nvote = parse_votes(fopen(vote_fname,"r"), &votes, cands, ncand, groups, ngroup, strict);
	elected = malloc(nelect*sizeof(int));
	run_stv(votes, nvote, cands, ncand, elected, nelect);

	printf("Elected %2d candidates for %s:\n", nelect, area);
	for(i=0;i<nelect;i++) {
		printf( "%s (%s)\n", cands[elected[i]].name, groups[cands[elected[i]].group].name);
	}
	return 0;
}
