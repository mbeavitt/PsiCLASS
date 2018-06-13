PsiCLASS
=======

Described in: 

xxxx

PsiCLASS includes source code from the [`samtools`](https://github.com/samtools/samtools). 

### What is PsiCLASS?

PsiCLASS is a reference-based transcriptome assembler for single or multiple RNA-seq samples. 

### Install

1. Clone the [GitHub repo](https://github.com/mourisl/class3), e.g. with `git clone https://github.com/mourisl/class3.git`
2. Run `make` in the repo directory

CLASS3 depends on [pthreads](http://en.wikipedia.org/wiki/POSIX_Threads) and samtools dependends on [zlib](http://en.wikipedia.org/wiki/Zlib).


### Usage

Usage: ./psiclass [OPTIONS]
	Required:
		-b STRING: the path to the BAM files. Use comma to separate ultiple bam files
			or
		--lb STRING: the path to the file listing the alignments bam files
	Optional:
		-s STRING: the path to the trusted splice sites file (default: not used)
		-o STRING: the prefix of output files (default: ./psiclass)
		-t INT: number of threads (default: 1)
		--hasMateIdSuffix: the read id has suffix such as .1, .2 for a mate pair. (default: false)
		--stage INT:  (default: 0)
			0-start from beginning (building splice sites for each sample)
			1-start from building subexon files for each sample
			2-start from combining subexon files
			3-start from assembly the transcripts
			4-start from voting the consensus transcripts

### Output

In your current directory or the directory specified by -o, you will find the output files of PsiCLASS:

	Sample-wise GTF files: (psiclass)_sample_{0,1,...,n-1}.gtf
	Meta-assembly GTF file: (psiclass)_vote.gtf

You will also find other files generated by PsiCLASS in several subdirectories:

	Intron files: splice/*
	Subexon graph files: subexon/*


### Example


### Terms of use

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received (LICENSE.txt) a copy of the GNU General
Public License along with this program; if not, you can obtain one from
http://www.gnu.org/licenses/gpl.txt or by writing to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 
### Support

Create a [GitHub issue](https://github.com/mourisl/classes/issues).
