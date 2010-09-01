#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "bio.h"
#include "sim4.H"


char const *usage =
"usage: %s [-c c] [-i i] [-o o]\n"
"  -verbose       Report progress\n"
"\n"
"  -c c           Discard polishes below c%% composite (default: 0).\n"
"  -i i           Discard polishes below i%% identity (default: 0).\n"
"  -l l           Discard polishes below l identities (default: 0).\n"
"\n"
"  -minexons e    Discard polishes below e exons (default: 0).\n"
"  -maxexons e    Discard polishes above e exons (default: infinity).\n"
"\n"
"  -C c           Discard polishes that are not from cDNA idx 'c'\n"
"  -G g           Discard polishes that are not from genomic idx 'g'\n"
"\n"
"  -o o           Write saved polishes to the 'o' file (default == stdout).\n"
"  -O             Don't write saved polishes.\n"
"\n"
"  -d o           Write discarded polishes to the 'o' file (default == stdout).\n"
"  -D             Don't write discarded polishes.\n"
"\n"
"  -j o           Write intractable and aborted polishes to the 'o' file.  By\n"
"                 default these are silently discarded.\n"
"\n"
"  -selfhits      Filter out alignments to ourself -- if you did an all-to-all\n"
"                 mapping of a set onto itself.  Deflines needed!\n"
"\n"
"  -segregate a b Segregate polishes by genomic idx, for idx's between a and b inclusive.\n"
"                 b-a must be less than %u.\n"
"                 Must be used with -o.\n"
"                 Will create numerous files 'o.%%05d'.\n"
"\n"
"  -nodeflines    Strip out deflines.\n"
"  -noalignments  Strip out alignments.\n"
"  -normalized    Strip out the genomic region (makes the polish relative\n"
"                 to the start of the sequence).\n"
"\n"
"                 All conditions must be met.\n";


int
main(int argc, char ** argv) {
  u32bit       arg  = 1;
  u32bit       minC = 0;
  u32bit       minI = 0;
  u32bit       minL = 0;
  u32bit       cdna = ~u32bitZERO;
  u32bit       geno = ~u32bitZERO;
  u32bit       minExons = 0;
  u32bit       maxExons = ~u32bitZERO;
  u32bit       beVerbose = 0;
  int          GOODsilent = 0;
  FILE        *GOOD       = stdout;
  int          CRAPsilent = 0;
  FILE        *CRAP       = stdout;
  FILE        *JUNK = 0L;
  sim4polish  *p;
  u64bit       pmod = 1;
  u64bit       good = 0;
  u64bit       crap = 0;
  u64bit       junk = 0;
  int          doSelfFilter = 0;
  int          doSegregation = 0;
  u32bit       doSegregationLo = 0;
  u32bit       doSegregationHi = 0;
  char        *filePrefix = 0L;
  FILE       **SEGREGATE = 0L;
  u32bit       printOpts = S4P_PRINTPOLISH_NOTVALUABLE;

  //  We limit scaffolds to be below the number of open files per
  //  process.
  //
  u32bit       maxScaffold = sysconf(_SC_OPEN_MAX);

  arg = 1;
  while (arg < argc) {
    if        (strncmp(argv[arg], "-verbose", 2) == 0) {
      beVerbose = 1;
    } else if (strncmp(argv[arg], "-c", 2) == 0) {
      minC = atoi(argv[++arg]);
    } else if (strncmp(argv[arg], "-i", 2) == 0) {
      minI = atoi(argv[++arg]);
    } else if (strncmp(argv[arg], "-l", 2) == 0) {
      minL = atoi(argv[++arg]);
    } else if (strncmp(argv[arg], "-minexons", 3) == 0) {
      minExons = atoi(argv[++arg]);
    } else if (strncmp(argv[arg], "-maxexons", 3) == 0) {
      maxExons = atoi(argv[++arg]);
    } else if (strncmp(argv[arg], "-o", 2) == 0) {
      arg++;
      errno = 0;
      filePrefix = argv[arg];
      GOOD = fopen(argv[arg], "w");
      if (errno) {
        fprintf(stderr, "error: I couldn't open '%s' for saving good polishes.\n%s\n", argv[arg], strerror(errno));
        exit(1);
      }
      GOODsilent = 0;
    } else if (strncmp(argv[arg], "-O", 2) == 0) {
      GOODsilent = 1;
    } else if (strncmp(argv[arg], "-d", 2) == 0) {
      arg++;
      errno = 0;
      CRAP = fopen(argv[arg], "w");
      if (errno) {
        fprintf(stderr, "error: I couldn't open '%s' for saving discarded polishes.\n%s\n", argv[arg], strerror(errno));
        exit(1);
      }
      CRAPsilent = 0;
    } else if (strncmp(argv[arg], "-q", 2) == 0) {
      CRAPsilent = 1;
    } else if (strncmp(argv[arg], "-D", 2) == 0) {
      CRAPsilent = 1;
    } else if (strncmp(argv[arg], "-j", 2) == 0) {
      arg++;
      errno = 0;
      JUNK = fopen(argv[arg], "w");
      if (errno) {
        fprintf(stderr, "error: I couldn't open '%s' for saving junk polishes.\n%s\n", argv[arg], strerror(errno));
        exit(1);
      }
    } else if (strncmp(argv[arg], "-C", 2) == 0) {
      cdna = atoi(argv[++arg]);
    } else if (strncmp(argv[arg], "-G", 2) == 0) {
      geno = atoi(argv[++arg]);
    } else if (strncmp(argv[arg], "-selfhits", 4) == 0) {
      doSelfFilter = 1;
    } else if (strncmp(argv[arg], "-segregate", 4) == 0) {
      doSegregation = 1;
      doSegregationLo = atoi(argv[++arg]);
      doSegregationHi = atoi(argv[++arg]);
      if (doSegregationHi - doSegregationLo + 1 > maxScaffold)
        fprintf(stderr, "error: -segregate range too big; must be less than %u.\n", maxScaffold), exit(1);
      SEGREGATE = (FILE **)calloc(maxScaffold, sizeof(FILE *));
    } else if (strncmp(argv[arg], "-nodeflines", 4) == 0) {
      printOpts |= S4P_PRINTPOLISH_NODEFS;
    } else if (strncmp(argv[arg], "-noalignments", 4) == 0) {
      printOpts |= S4P_PRINTPOLISH_NOALIGNS;
    } else {
      fprintf(stderr, "UNKNOWN option '%s'\n", argv[arg]);
      exit(1);
    }

    arg++;
  }

  if (isatty(fileno(stdin))) {
    fprintf(stderr, usage, argv[0], maxScaffold);
    exit(1);
  }

  if (!CRAPsilent && !GOODsilent && (fileno(GOOD) == fileno(CRAP))) {
    fprintf(stderr, "error: filter has no effect; saved and discarded polishes\n");
    fprintf(stderr, "       both printed to the same place!\n");
    fprintf(stderr, "       (try using one of -o, -O, -d, -D)\n");
    exit(1);
  }

  if (doSegregation && (filePrefix == 0L)) {
    fprintf(stderr, "error: you must specify a file prefix when segregating (-s requires -o)\n");
    exit(1);
  }


  if (beVerbose) {
    fprintf(stderr, "Filtering at "u32bitFMT"%% coverage and "u32bitFMT"%% identity and "u32bitFMT"bp.\n", minC, minI, minL);

    if ((cdna != ~u32bitZERO) && (cdna != ~u32bitZERO))
      fprintf(stderr, "Filtering for cDNA idx "u32bitFMT" and genomic idx "u32bitFMT"\n", cdna, geno);
    else if (cdna != ~u32bitZERO)
      fprintf(stderr, "Filtering for cDNA idx "u32bitFMT".\n", cdna);
    else if (geno != ~u32bitZERO)
      fprintf(stderr, "Filtering for genomic idx "u32bitFMT".\n", geno);
  }

  while ((p = s4p_readPolish(stdin)) != 0L) {

    if (JUNK && ((p->strandOrientation == SIM4_STRAND_INTRACTABLE) ||
                 (p->strandOrientation == SIM4_STRAND_FAILED))) {
      junk++;
      s4p_printPolish(JUNK, p, printOpts);
    } else {
      if ((p->percentIdentity  >= minI) &&
          (p->querySeqIdentity >= minC) &&
          (p->numMatches  >= minL) &&
          ((cdna == -1) || (cdna == p->estID)) &&
          ((geno == -1) || (geno == p->genID)) &&
          (minExons <= p->numExons) &&
          (p->numExons <= maxExons) &&
          ((doSelfFilter == 0) || (strcmp(p->estDefLine, p->genDefLine) != 0))) {
        good++;
        if (doSegregation) {
          if ((doSegregationLo <= p->genID) &&
              (p->genID <= doSegregationHi)) {
            if (SEGREGATE[p->genID - doSegregationLo] == 0L) {
              char filename[1024];
              sprintf(filename, "%s.%04d", filePrefix, (int)p->genID);
              errno = 0;
              SEGREGATE[p->genID - doSegregationLo] = fopen(filename, "w");
              if (errno) {
                fprintf(stderr, "Error: Couldn't open '%s'\n%s\n", filename, strerror(errno));
                exit(1);
              }
            }
            s4p_printPolish(SEGREGATE[p->genID - doSegregationLo], p, printOpts);
          }
        } else {
          if (!GOODsilent)
            s4p_printPolish(GOOD, p, printOpts);
        }
      } else {
        crap++;
        if (!CRAPsilent)
          s4p_printPolish(CRAP, p, printOpts);
      }
    }

    if ((beVerbose) && ((good+crap) == pmod)) {
      pmod += 8888 + (random() % 1000);
      if (junk > 0)
        fprintf(stderr, " Filter: %6.2f%% ("u64bitFMT" matches processed) ("u64bitFMT" failed/intractable)\r",
                100.0 * good / (good+crap),
                good+crap,
                junk);
      else
        fprintf(stderr, " Filter: %6.2f%% ("u64bitFMT" matches processed)\r",
                100.0 * good / (good+crap),
                good+crap);
      fflush(stderr);
    }

    s4p_destroyPolish(p);
  }

  if (beVerbose) {
    if (junk > 0)
      fprintf(stderr, " Filter: %6.2f%% ("u64bitFMT" matches processed) ("u64bitFMT" failed/intractable)\n",
              100.0 * good / (good+crap),
              good+crap,
              junk);
    else
      fprintf(stderr, " Filter: %6.2f%% ("u64bitFMT" matches processed)\n",
              100.0 * good / (good+crap),
              good+crap);
  }

  if (GOOD)
    fclose(GOOD);
  if (JUNK)
    fclose(JUNK);

  return(0);
}
