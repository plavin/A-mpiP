#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include "mpiP-stats.h"
#include "mpiPi.h"

/*
 * ============================================================================
 *
 * Histogram code
 *
 * ============================================================================
 */

static int
get_histogram_bin (mpiPi_histogram_t * h, int val)
{
  int wv = val;
  int bin;

  bin = 0;

  if (h->bin_intervals == NULL)
    {
      while (wv > h->first_bin_max && bin < h->hist_size)
        {
          wv >>= 1;
          bin++;
        }
    }
  else   /* Add code for custom intervals later */
    {
    }

  return bin;
}


void
init_histogram (mpiPi_histogram_t * h, int first_bin_max, int size,
                int *intervals)
{
  h->first_bin_max = first_bin_max;
  h->hist_size = size;
  h->bin_intervals = intervals;
}

void
get_histogram_bin_str (mpiPi_histogram_t * h, int v, char *s)
{
  int min = 0, max = 0;

  if (v == 0)
    {
      min = 0;
      max = h->first_bin_max;
    }
  else
    {
      min = h->first_bin_max + 1;
      min <<= (v - 1);
      max = (min << 1) - 1;
    }

  sprintf (s, "%8d - %8d", min, max);
}

/*
 * ============================================================================
 * Custom string append function
 * ============================================================================
 */

// str: string to append to
// fmt: the format string to append to str
// ...: arguments to fmt
// Return: the new length of str or -1 if there was an error
int strappend(char **str, const char *fmt, ...)
{
  // Step 1: Use snprintf to get the length of the format string
  va_list args;
  va_start(args, fmt);
  int fmt_len = vsnprintf(NULL, 0, fmt, args)+1;
  va_end(args);
  if (fmt_len < 0){
    return -1; //error
  }

  // Step 2: Re-alloc str
  if (!*str) {
    *str = (char*)malloc(sizeof(char)*1);
    *str[0] = '\0';
  }
  int old_len = strlen(*str);
  *str = realloc(*str, old_len + fmt_len + 1);

  // Step 3: Alloc space for fmt_str
  char *fmt_str = (char*)malloc(fmt_len*sizeof(char)+1);
  va_start(args, fmt);
  vsnprintf(fmt_str, fmt_len, fmt, args);
  va_end(args);


  // Step 4: Concatenate strings and free fmt_str
  strcat(*str, fmt_str);
  free(fmt_str);
  return old_len + fmt_len;
}

/*
 * ============================================================================
 *
 * Per-thread statistics
 *
 * ============================================================================
 */


static int
_thrd_pc_hashkey (const void *p)
{
  int res = 0;
  int i;
  callsite_stats_t *csp = (callsite_stats_t *) p;
  MPIP_CALLSITE_STATS_COOKIE_ASSERT (csp);
  for (i = 0; i < mpiPi.fullStackDepth; i++)
    {
      res ^= (unsigned) (long) csp->pc[i];
    }
  return 52271 ^ csp->op ^ res ^ csp->rank;
}

static int
trd_pc_comparator (const void *p1, const void *p2)
{
  int i;
  callsite_stats_t *csp_1 = (callsite_stats_t *) p1;
  callsite_stats_t *csp_2 = (callsite_stats_t *) p2;
  MPIP_CALLSITE_STATS_COOKIE_ASSERT (csp_1);
  MPIP_CALLSITE_STATS_COOKIE_ASSERT (csp_2);

#define express(f) {if ((csp_1->f) > (csp_2->f)) {return 1;} if ((csp_1->f) < (csp_2->f)) {return -1;}}
  express (op);
  express (rank);

  for (i = 0; i < mpiPi.fullStackDepth; i++)
    {
      express (pc[i]);
    }
#undef express

  return 0;
}

void mpiPi_stats_thr_init(mpiPi_thread_stat_t *stat)
{
  stat->cs_stats = h_open (mpiPi.tableSize, _thrd_pc_hashkey,
                                      trd_pc_comparator);

  bzero(stat->coll.time_stats, sizeof(stat->coll.time_stats));
  if (mpiPi.do_collective_stats_report == 1)
    {
      init_histogram (&stat->coll.comm_hist, 7, MPIP_COMM_HISTCNT, NULL);
      init_histogram (&stat->coll.size_hist, 7, MPIP_SIZE_HISTCNT, NULL);
    }

  bzero(stat->pt2pt.time_stats, sizeof(stat->pt2pt.time_stats));
  if (mpiPi.do_pt2pt_stats_report == 1)
    {
      init_histogram (&stat->pt2pt.comm_hist, 7, MPIP_COMM_HISTCNT, NULL);
      init_histogram (&stat->pt2pt.size_hist, 7, MPIP_SIZE_HISTCNT, NULL);
    }
}

void mpiPi_stats_thr_fini(mpiPi_thread_stat_t *stat)
{
  h_close (stat->cs_stats);
}

void mpiPi_stats_thr_reset_all(mpiPi_thread_stat_t *s)
{
  /* Reset callsite statistics */
  mpiPi_stats_thr_cs_reset(s);
  bzero(s->coll.time_stats, sizeof(s->coll.time_stats));
  (s->pt2pt.time_stats, sizeof(s->pt2pt.time_stats));
  s->cum_time = 0;
}

void mpiPi_stats_thr_merge_all(mpiPi_thread_stat_t *dst,
                               mpiPi_thread_stat_t *src)
{
  mpiPi_stats_thr_cs_merge(dst, src);
  mpiPi_stats_thr_coll_merge(dst, src);
  mpiPi_stats_thr_pt2pt_merge(dst, src);
  dst->cum_time += src->cum_time;
}

static inline double _get_duration(mpiPi_thread_stat_t *s)
{
  double dur = 0.0;
  dur = (mpiPi_GETTIMEDIFF (&s->ts_end, &s->ts_start) / 1000000.0);
  return dur;
}

void mpiPi_stats_thr_timer_start(mpiPi_thread_stat_t *s)
{
  mpiPi_GETTIME (&s->ts_start);
}
void mpiPi_stats_thr_timer_stop(mpiPi_thread_stat_t *s)
{
  mpiPi_GETTIME (&s->ts_end);
  s->cum_time += _get_duration(s);
  s->prev_csid = 0;
  s->prev_time = s->ts_end;
}

double mpiPi_stats_thr_cum_time(mpiPi_thread_stat_t *s)
{
  return s->cum_time;
}

void mpiPi_stats_thr_exit(mpiPi_thread_stat_t *stat)
{
  stat->disabled++;
}

void mpiPi_stats_thr_enter(mpiPi_thread_stat_t *stat)
{
  stat->disabled--;
}

int mpiPi_stats_thr_is_on(mpiPi_thread_stat_t *stat)
{
  return !(stat->disabled) && mpiPi.enabled;
}

void
mpiPi_stats_thr_cs_upd (mpiPi_thread_stat_t *stat,
                        unsigned op, unsigned rank, void **pc,
                        double dur, double sendSize, double ioSize,
                        double rmaSize, int isColl, MPI_Comm *comm,
                        int dest,
                        const int *sendcount,
                        const int *recvcount)
{
  int i;
  callsite_stats_t *csp = NULL;
  callsite_stats_t key;

  assert (dur >= 0);

  /* Check for the nested calls */
  if (!mpiPi_stats_thr_is_on(stat))
    return;

  /* only collect from rank 0 unles specified */
  if ((rank != 0) && (traceAllRanks == 0))
      return;

  key.op = op;
  key.rank = rank;
  key.cookie = MPIP_CALLSITE_STATS_COOKIE;
  for (i = 0; i < mpiPi.fullStackDepth; i++)
    {
      key.pc[i] = pc[i];
    }

  if (NULL == h_search (stat->cs_stats, &key, (void **) &csp))
    {
      /* create and insert */
      csp = (callsite_stats_t *) malloc (sizeof (callsite_stats_t));
      bzero (csp, sizeof (callsite_stats_t));
      mpiPi_cs_init(csp, pc, op, rank);
      h_insert (stat->cs_stats, csp);
    }
  /* ASSUME: csp cannot be deleted from list */
  mpiPi_cs_update(csp, dur, sendSize, ioSize, rmaSize,
                  mpiPi.messageCountThreshold);

  /* TRACE */
  {
      // PATRICK
      /*
      int i, ac;
      callsite_stats_t *av;
      h_search(callsite_src_id_cache, --, (void **) &av);
      */
      //h_gather_data(callsite_src_id_cache, &ac, (void ***) &av);
      // END PATRICK

      /* PATRICK 2
      int hashval = mpiPi_query_csp_hash(csp);
      if (hashval == 0) {
        printf("bummer\n");
        exit(1);
      }
      */


    // build entire line at once so that they whole string gets printed to the file at once

      mpiPi_TIME now;
      double dur;
      mpiPi_GETTIME (&now);
      dur = mpiPi_GETTIMEDIFF (&now, &(stat->prev_time));
      /*
      int mtx_ret = mtx_lock(&trace_mtx);
      if (mtx_ret != thrd_success) {
        printf("Error calling mtx_lock\n");
        exit(1);
      }
      */


      char *trace_str = NULL;
      char *tmp_str;
      strappend(&trace_str, "TRACE %d -> %d %.1f %s ", stat->prev_csid, csp->tmpid, dur, mpiPi.lookup[op - mpiPi_BASE].name);

      //fprintf(tracefile, "TRACE %d -> %d %.1f %s ", stat->prev_csid, csp->tmpid, dur,
       //      mpiPi.lookup[op - mpiPi_BASE].name);

      stat->prev_csid = csp->tmpid;
      stat->prev_time = now;

      {
          // get global and this group for translation
          MPI_Group worldGroup, thisGroup;
          // Do not to the translation if the comm object is null
          // or if it has been set to MPI_COMM_NULL by MPI_Comm_free
          int doTrans = (comm != NULL) && (*comm != MPI_COMM_NULL);
          if (doTrans) {
              if (PMPI_Comm_group(MPI_COMM_WORLD, &worldGroup) != MPI_SUCCESS) {
                  strappend(&trace_str, "MPI Comm Group Error\n");
                 //fprintf(tracefile, "MPI Comm Group  Error\n");
              }
              if (PMPI_Comm_group(*comm, &thisGroup) != MPI_SUCCESS) {
                  strappend(&trace_str, "MPI Comm Group Error\n");
                  //fprintf(tracefile, "MPI Comm Group Error\n");
              }
          }

          // mpi trace
          if (isColl) {
              int nranks, *ranks, *gRanksOut, i;

              // translate this group to global ranks
              if (doTrans) {
                  PMPI_Group_size(thisGroup, &nranks);
                  ranks = (int*)malloc(sizeof(int) * nranks);
                  gRanksOut = (int*)malloc(sizeof(int) * nranks);
                  for(i=0; i < nranks; ++i) {
                      ranks[i] = i;
                  }
                  PMPI_Group_translate_ranks(thisGroup, nranks, ranks,
                                             worldGroup, gRanksOut);

                  strappend(&trace_str, "coll %p %.0f to ", *comm, sendSize);

                  //fprintf(tracefile, "coll %p %.0f to ", *comm, sendSize);
                  for(i=0; i < nranks; ++i) {
                      strappend(&trace_str, "%d", gRanksOut[i]);
                      //fprintf(tracefile, "%d", gRanksOut[i]);
                      if (sendcount) {
                        strappend(&trace_str, ">%d", sendcount[i]);
                        //fprintf(tracefile, ">%d", sendcount[i]);
                      }
                      if (recvcount) {
                        strappend(&trace_str, "<%d", recvcount[i]);
                        //fprintf(tracefile, "<%d", recvcount[i]);
                      }
                      strappend(&trace_str, " ");
                      //fprintf(tracefile, " ");
                  }
              }
              strappend(&trace_str, "\n");
              //fprintf(tracefile, "\n");

              if (doTrans) {
                  free(ranks);
                  free(gRanksOut);
              }
          } else {
              if (doTrans && dest != -1) {
                  // pt2pt
                  int outRank;
                  // translate the destination rank to global
                  PMPI_Group_translate_ranks(thisGroup, 1, &dest,
                                             worldGroup, &outRank);

                  strappend(&trace_str, "p2p %.0f to %d:%d\n", sendSize, dest, outRank);

//                  fprintf(tracefile, "p2p %.0f to %d:%d\n", sendSize,
 //                        dest, outRank);
              } else {
                  strappend(&trace_str, "\n");
                  //fprintf(tracefile, "\n");
              }
          }

          if (doTrans) {
              PMPI_Group_free(&worldGroup);
              PMPI_Group_free(&thisGroup);
          }
      }

      //mtx_lock(&trace_mtx);
      fprintf(tracefile, trace_str);
      //mtx_unlock(&trace_mtx);
      free(trace_str);
      /*
      mtx_ret = mtx_unlock(&trace_mtx);
      if (mtx_ret != thrd_success) {
        printf("Error calling mtx_unlock\n");
        exit(1);
      }
      */

      // reset time so we don't include trace time
      mpiPi_GETTIME (&now);
      stat->prev_time = now;
  }

#if 0
  mpiPi_msg_debug ("mpiPi.messageCountThreshold is %d\n",
                   mpiPi.messageCountThreshold);
  mpiPi_msg_debug ("sendSize is %f\n", sendSize);
  mpiPi_msg_debug ("csp->arbitraryMessageCount is %lld\n",
                   csp->arbitraryMessageCount);
#endif
  return;
}

void mpiPi_stats_thr_cs_gather(mpiPi_thread_stat_t *stat,
                             int *ac, callsite_stats_t ***av )
{
  h_gather_data (stat->cs_stats, ac, (void ***)av);
}

void mpiPi_stats_thr_cs_reset(mpiPi_thread_stat_t *stat)
{
  int ac, ndx;
  callsite_stats_t **av;
  callsite_stats_t *csp = NULL;

  /* gather local task data */
  h_drain(stat->cs_stats, &ac, (void ***)&av);

  for (ndx = 0; ndx < ac; ndx++)
    {
      free(av[ndx]);
    }
  free(av);
}

void mpiPi_stats_thr_cs_lookup(mpiPi_thread_stat_t *stat,
                              callsite_stats_t *task_stats,
                              callsite_stats_t **task_lookup,
                              callsite_stats_t *dummy_buf,
                              int initMax)
{
  callsite_stats_t *record = NULL;
  if (NULL == h_search(stat->cs_stats,
                       task_stats,(void **)&record))
    {
      record = dummy_buf;
      mpiPi_cs_reset_stat(record);
      if (!initMax) {
          record->minDur = 0;
          record->minDataSent = 0;
          record->minIO = 0;
        }
      record->rank = mpiPi.rank;
    }
  *task_lookup = record;
}

void mpiPi_stats_thr_cs_merge(mpiPi_thread_stat_t *dst,
                              mpiPi_thread_stat_t *src)
{
  int ac, i;
  callsite_stats_t **av;
  /* Merge callsite statistics */
  mpiPi_stats_thr_cs_gather(src, &ac, &av);
  for(i=0; i<ac; i++)
    {
      callsite_stats_t *csp_src = av[i], *csp_dst;

      /* Search for the callsite and create a new record if needed */
      if (NULL == h_search (dst->cs_stats, csp_src, (void **) &csp_dst))
        {
          /* create and insert */
          csp_dst = (callsite_stats_t *) malloc (sizeof (callsite_stats_t));
          bzero (csp_dst, sizeof (callsite_stats_t));
          mpiPi_cs_init(csp_dst, csp_src->pc, csp_src->op, csp_src->rank);
          h_insert (dst->cs_stats, csp_dst);
        }
      /* Merge callsite records */
      mpiPi_cs_merge(csp_dst, csp_src);
    }

  free (av);
}

/* Collective histogram stats track call timing */
static void _update_dur_stat (mpiPi_msg_stat_t *stat,
                                int op, double dur, double size,
                                MPI_Comm * comm,
                                char *dbg_prefix)
{
  int op_idx, comm_size, comm_bin, size_bin;

  PMPI_Comm_size (*comm, &comm_size);

  op_idx = op - mpiPi_BASE;

  comm_bin = get_histogram_bin (&stat->comm_hist, comm_size);
  size_bin = get_histogram_bin (&stat->size_hist, size);

  mpiPi_msg_debug
      ("Adding %.0f time to entry %s[%d][%d][%d] value of %.0f\n",
       dur, dbg_prefix,
       op_idx, comm_bin, size_bin,
       stat->time_stats[op_idx][comm_bin][size_bin]);

  stat->time_stats[op_idx][comm_bin][size_bin] += dur;
}

/* Message size statistics */
static void _gather_msize_stat(mpiPi_msg_stat_t *stat,
                               double **_outbuf)
{
  double *outbuf = malloc(sizeof(stat->time_stats));
  memcpy(outbuf, stat->time_stats, sizeof(stat->time_stats));
  *_outbuf = outbuf;
}

static void _update_msize_stat (mpiPi_msg_stat_t *stat,
                                int op, double dur, double size,
                                MPI_Comm * comm,
                                char *dbg_prefix)
{
  int op_idx, comm_size, comm_bin, size_bin;

  PMPI_Comm_size (*comm, &comm_size);

  op_idx = op - mpiPi_BASE;

  comm_bin = get_histogram_bin (&stat->comm_hist, comm_size);
  size_bin = get_histogram_bin (&stat->size_hist, size);

  mpiPi_msg_debug
      ("Adding %.0f send size to entry %s[%d][%d][%d] value of %.0f\n",
       size, dbg_prefix,
       op_idx, comm_bin, size_bin,
       stat->time_stats[op_idx][comm_bin][size_bin]);

  stat->time_stats[op_idx][comm_bin][size_bin] += size;
}

static void _merge_msize_stat (mpiPi_msg_stat_t *dst, mpiPi_msg_stat_t *src)
{
  int i = 0, x, y, z;
  for (x = 0; x < MPIP_NFUNC; x++)
    for (y = 0; y < MPIP_COMM_HISTCNT; y++)
      for (z = 0; z < MPIP_SIZE_HISTCNT; z++)
        dst->time_stats[x][y][z] += src->time_stats[x][y][z];
}

static void _get_binstrings (mpiPi_msg_stat_t *stat,
                             int comm_idx, char *comm_buf,
                             int size_idx, char *size_buf)
{
  get_histogram_bin_str (&stat->comm_hist, comm_idx, comm_buf);
  get_histogram_bin_str (&stat->size_hist, size_idx, size_buf);
}


/* Collectives msg size stat */
void mpiPi_stats_thr_coll_upd (mpiPi_thread_stat_t *stat,
                                  int op, double dur, double size,
                                  MPI_Comm * comm)
{
  /* Check for the nested calls */
  if (!mpiPi_stats_thr_is_on(stat))
    return;

  _update_dur_stat(&stat->coll, op, dur, size, comm, "collectives");
}

void mpiPi_stats_thr_coll_gather(mpiPi_thread_stat_t *stat, double **_outbuf)
{
  _gather_msize_stat(&stat->coll, _outbuf);
}

void mpiPi_stats_thr_coll_merge(mpiPi_thread_stat_t *dst,
                                mpiPi_thread_stat_t *src)
{
  _merge_msize_stat(&dst->coll, &src->coll);
}

void mpiPi_stats_thr_coll_binstrings(mpiPi_thread_stat_t *stat,
                                     int comm_idx, char *comm_buf,
                                     int size_idx, char *size_buf)
{
  _get_binstrings(&stat->coll, comm_idx, comm_buf, size_idx, size_buf);
}


/* Point-to-point statistics */
void mpiPi_stats_thr_pt2pt_upd (mpiPi_thread_stat_t *stat,
                                   int op, double dur, double size,
                                   MPI_Comm * comm)
{
  /* Check for the nested calls */
  if (!mpiPi_stats_thr_is_on(stat))
    return;

  _update_msize_stat(&stat->pt2pt, op, dur, size, comm, "point-to-point");
}

void mpiPi_stats_thr_pt2pt_gather(mpiPi_thread_stat_t *stat, double **_outbuf)
{
  _gather_msize_stat(&stat->pt2pt, _outbuf);
}

void mpiPi_stats_thr_pt2pt_merge(mpiPi_thread_stat_t *dst,
                                mpiPi_thread_stat_t *src)
{
  _merge_msize_stat(&dst->pt2pt, &src->pt2pt);
}

void mpiPi_stats_thr_pt2pt_binstrings(mpiPi_thread_stat_t *stat,
                                     int comm_idx, char *comm_buf,
                                     int size_idx, char *size_buf)
{
  _get_binstrings(&stat->pt2pt, comm_idx, comm_buf, size_idx, size_buf);
}

/*

  <license>

  Copyright (c) 2006, The Regents of the University of California.
  Produced at the Lawrence Livermore National Laboratory
  Written by Jeffery Vetter and Christopher Chambreau.
  UCRL-CODE-223450.
  All rights reserved.

  Copyright (c) 2019, Mellanox Technologies Inc.
  Written by Artem Polyakov
  All rights reserved.


  This file is part of mpiP.  For details, see http://llnl.github.io/mpiP.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

  * Redistributions of source code must retain the above copyright
  notice, this list of conditions and the disclaimer below.

  * Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the disclaimer (as noted below) in
  the documentation and/or other materials provided with the
  distribution.

  * Neither the name of the UC/LLNL nor the names of its contributors
  may be used to endorse or promote products derived from this software
  without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OF
  THE UNIVERSITY OF CALIFORNIA, THE U.S. DEPARTMENT OF ENERGY OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


  Additional BSD Notice

  1. This notice is required to be provided under our contract with the
  U.S. Department of Energy (DOE).  This work was produced at the
  University of California, Lawrence Livermore National Laboratory under
  Contract No. W-7405-ENG-48 with the DOE.

  2. Neither the United States Government nor the University of
  California nor any of their employees, makes any warranty, express or
  implied, or assumes any liability or responsibility for the accuracy,
  completeness, or usefulness of any information, apparatus, product, or
  process disclosed, or represents that its use would not infringe
  privately-owned rights.

  3.  Also, reference herein to any specific commercial products,
  process, or services by trade name, trademark, manufacturer or
  otherwise does not necessarily constitute or imply its endorsement,
  recommendation, or favoring by the United States Government or the
  University of California.  The views and opinions of authors expressed
  herein do not necessarily state or reflect those of the United States
  Government or the University of California, and shall not be used for
  advertising or product endorsement purposes.

  </license>

*/

/* EOF */
