/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/* \file nr_ue_dci_configuration.c
 * \brief functions for generating dci search procedures based on RRC Serving Cell Group Configuration
 * \author R. Knopp, G. Casati
 * \date 2020
 * \version 0.2
 * \company Eurecom, Fraunhofer IIS
 * \email: knopp@eurecom.fr, guido.casati@iis.fraunhofer.de
 * \note
 * \warning
 */

#include "mac_proto.h"
#include "mac_defs.h"
#include "assertions.h"
#include "LAYER2/NR_MAC_UE/mac_extern.h"
#include "mac_defs.h"
#include "common/utils/nr/nr_common.h"
#include "executables/softmodem-common.h"
#include <stdio.h>
#include "nfapi_nr_interface.h"

void fill_dci_search_candidates(const NR_SearchSpace_t *ss,
                                fapi_nr_dl_config_dci_dl_pdu_rel15_t *rel15,
                                const uint32_t Y)
{
  LOG_D(NR_MAC,"Filling search candidates for DCI\n");

  rel15->number_of_candidates = 0;
  int i = 0;
  for (int maxL = 16; maxL > 0; maxL >>= 1) {
    uint8_t aggregation, number_of_candidates;
    find_aggregation_candidates(&aggregation,
                                &number_of_candidates,
                                ss,
                                maxL);
    if (number_of_candidates > 0) {
      LOG_D(NR_MAC,"L %d, number of candidates %d, aggregation %d\n", maxL, number_of_candidates, aggregation);
      int N_cce_sym = 0; // nb of rbs of coreset per symbol
      for (int f = 0; f < 6; f++) {
        for (int t = 0; t < 8; t++) {
          N_cce_sym += ((rel15->coreset.frequency_domain_resource[f] >> t) & 1);
        }
      }
      int N_cces = N_cce_sym * rel15->coreset.duration;
      // limit the number of candidates to the ones fitting current configuration
      int max_candidates = min(N_cces / aggregation, number_of_candidates);
      rel15->number_of_candidates += max_candidates;
      for (int j = 0; j < max_candidates; i++, j++) {
        int first_cce = aggregation * ((Y + ((j * N_cces) / (aggregation * number_of_candidates)) + 0) % (N_cces / aggregation));
        LOG_D(NR_MAC,"Candidate %d of %d first_cce %d (L %d N_cces %d Y %d)\n", j, max_candidates, first_cce, aggregation, N_cces, Y);
        rel15->CCE[i] = first_cce;
        rel15->L[i] = aggregation;
      }
    }
  }
}

NR_ControlResourceSet_t *ue_get_coreset(const NR_UE_MAC_INST_t *mac, const int coreset_id)
{
  NR_ControlResourceSet_t *coreset = NULL;
  for (int i = 0; i < FAPI_NR_MAX_CORESET_PER_BWP; i++) {
    if (mac->BWP_coresets[i] != NULL &&
        mac->BWP_coresets[i]->controlResourceSetId == coreset_id) {
      coreset = mac->BWP_coresets[i];
      break;
    }
  }
  AssertFatal(coreset, "Couldn't find coreset with id %d\n", coreset_id);
  return coreset;
}


void config_dci_pdu(NR_UE_MAC_INST_t *mac,
                    fapi_nr_dl_config_request_t *dl_config,
                    const int rnti_type,
                    const int slot,
                    const NR_SearchSpace_t *ss)
{

  const NR_UE_DL_BWP_t *current_DL_BWP = &mac->current_DL_BWP;
  const NR_UE_UL_BWP_t *current_UL_BWP = &mac->current_UL_BWP;
  NR_BWP_Id_t dl_bwp_id = current_DL_BWP ? current_DL_BWP->bwp_id : 0;

  fapi_nr_dl_config_dci_dl_pdu_rel15_t *rel15 = &dl_config->dl_config_list[dl_config->number_pdus].dci_config_pdu.dci_config_rel15;

  const int coreset_id = *ss->controlResourceSetId;
  NR_ControlResourceSet_t *coreset;
  if(coreset_id > 0) {
    LOG_E(NR_MAC, "CORESET>0 (%d)\n",coreset_id); //Abdallah Abou Hasna
    coreset = ue_get_coreset(mac, coreset_id);
    rel15->coreset.CoreSetType = NFAPI_NR_CSET_CONFIG_PDCCH_CONFIG;
  } else {
    LOG_E(NR_MAC, "CORESET=%d and SS = %ld\n", coreset_id, ss->searchSpaceId); //Abdallah Abou Hasna
    coreset = mac->coreset0;
    rel15->coreset.CoreSetType = NFAPI_NR_CSET_CONFIG_MIB_SIB1;
  }

  rel15->coreset.duration = coreset->duration;

  for (int i = 0; i < 6; i++)
    rel15->coreset.frequency_domain_resource[i] = coreset->frequencyDomainResources.buf[i];
  rel15->coreset.CceRegMappingType = coreset->cce_REG_MappingType.present ==
    NR_ControlResourceSet__cce_REG_MappingType_PR_interleaved ? FAPI_NR_CCE_REG_MAPPING_TYPE_INTERLEAVED : FAPI_NR_CCE_REG_MAPPING_TYPE_NON_INTERLEAVED;
  if (rel15->coreset.CceRegMappingType == FAPI_NR_CCE_REG_MAPPING_TYPE_INTERLEAVED) {
    struct NR_ControlResourceSet__cce_REG_MappingType__interleaved *interleaved = coreset->cce_REG_MappingType.choice.interleaved;
    rel15->coreset.RegBundleSize = (interleaved->reg_BundleSize == NR_ControlResourceSet__cce_REG_MappingType__interleaved__reg_BundleSize_n6) ? 6 : (2 + interleaved->reg_BundleSize);
    rel15->coreset.InterleaverSize = (interleaved->interleaverSize == NR_ControlResourceSet__cce_REG_MappingType__interleaved__interleaverSize_n6) ? 6 : (2 + interleaved->interleaverSize);
    rel15->coreset.ShiftIndex = interleaved->shiftIndex != NULL ? *interleaved->shiftIndex : mac->physCellId;
  } else {
    rel15->coreset.RegBundleSize = 0;
    rel15->coreset.InterleaverSize = 0;
    rel15->coreset.ShiftIndex = 0;
  }

  rel15->coreset.precoder_granularity = coreset->precoderGranularity;

  // Scrambling RNTI
  if (coreset->pdcch_DMRS_ScramblingID) {
    rel15->coreset.pdcch_dmrs_scrambling_id = *coreset->pdcch_DMRS_ScramblingID;
    rel15->coreset.scrambling_rnti = mac->crnti;
  } else {
    rel15->coreset.pdcch_dmrs_scrambling_id = mac->physCellId;
    rel15->coreset.scrambling_rnti = 0;
  }

  rel15->num_dci_options = (mac->ra.ra_state == WAIT_RAR ||
                           rnti_type == NR_RNTI_SI) ?
                           1 : 2;

  if (ss->searchSpaceType->present == NR_SearchSpace__searchSpaceType_PR_ue_Specific) {
    if (ss->searchSpaceType->choice.ue_Specific->dci_Formats ==
        NR_SearchSpace__searchSpaceType__ue_Specific__dci_Formats_formats0_0_And_1_0) {
      rel15->dci_format_options[0] = NR_DL_DCI_FORMAT_1_0;
      rel15->dci_format_options[1] = NR_UL_DCI_FORMAT_0_0;
    }
    else {
      rel15->dci_format_options[0] = NR_DL_DCI_FORMAT_1_1;
      rel15->dci_format_options[1] = NR_UL_DCI_FORMAT_0_1;
    }
  }
  else { // common
    AssertFatal(ss->searchSpaceType->choice.common->dci_Format0_0_AndFormat1_0,
                "Only supporting format 10 and 00 for common SS\n");
    rel15->dci_format_options[0] = NR_DL_DCI_FORMAT_1_0;
    rel15->dci_format_options[1] = NR_UL_DCI_FORMAT_0_0;
  }

  // loop over RNTI type and configure resource allocation for DCI
  for (int i = 0; i < rel15->num_dci_options; i++) {
    rel15->dci_type_options[i] = ss->searchSpaceType->present;
    const int dci_format = rel15->dci_format_options[i];
    uint16_t alt_size = 0;
    if(current_DL_BWP) {

      // computing alternative size for padding
      dci_pdu_rel15_t temp_pdu;
      if(dci_format == NR_DL_DCI_FORMAT_1_0)
        alt_size =
            nr_dci_size(current_DL_BWP, current_UL_BWP, mac->cg, &temp_pdu, NR_UL_DCI_FORMAT_0_0, rnti_type, coreset, dl_bwp_id, ss->searchSpaceType->present, mac->type0_PDCCH_CSS_config.num_rbs, 0);
      if(dci_format == NR_UL_DCI_FORMAT_0_0)
        alt_size =
            nr_dci_size(current_DL_BWP, current_UL_BWP, mac->cg, &temp_pdu, NR_DL_DCI_FORMAT_1_0, rnti_type, coreset, dl_bwp_id, ss->searchSpaceType->present, mac->type0_PDCCH_CSS_config.num_rbs, 0);
    }

    rel15->dci_length_options[i] = nr_dci_size(current_DL_BWP,
                                               current_UL_BWP,
                                               mac->cg,
                                               &mac->def_dci_pdu_rel15[dl_config->slot][dci_format],
                                               dci_format,
                                               rnti_type,
                                               coreset,
                                               dl_bwp_id,
                                               ss->searchSpaceType->present,
                                               mac->type0_PDCCH_CSS_config.num_rbs,
                                               alt_size);
  }

  rel15->BWPStart = coreset_id == 0 ? mac->type0_PDCCH_CSS_config.cset_start_rb : current_DL_BWP->BWPStart;
  rel15->BWPSize = coreset_id == 0 ? mac->type0_PDCCH_CSS_config.num_rbs : current_DL_BWP->BWPSize;

  uint16_t monitoringSymbolsWithinSlot = 0;
  int sps = 0;

  switch(rnti_type) {
    case NR_RNTI_C:
      // we use DL BWP dedicated
      sps = current_DL_BWP->cyclicprefix ? 12 : 14;
      // for SPS=14 8 MSBs in positions 13 down to 6
      monitoringSymbolsWithinSlot = (ss->monitoringSymbolsWithinSlot->buf[0]<<(sps-8)) | (ss->monitoringSymbolsWithinSlot->buf[1]>>(16-sps));
      rel15->rnti = mac->crnti;
      rel15->SubcarrierSpacing = current_DL_BWP->scs;
      break;
    case NR_RNTI_RA:
      // we use the initial DL BWP
      sps = current_DL_BWP->cyclicprefix == NULL ? 14 : 12;
      monitoringSymbolsWithinSlot = (ss->monitoringSymbolsWithinSlot->buf[0]<<(sps-8)) | (ss->monitoringSymbolsWithinSlot->buf[1]>>(16-sps));
      rel15->rnti = mac->ra.ra_rnti;
      rel15->SubcarrierSpacing = current_DL_BWP->scs;
      break;
    case NR_RNTI_P:
      break;
    case NR_RNTI_CS:
      break;
    case NR_RNTI_TC:
      // we use the initial DL BWP
      sps = current_DL_BWP->cyclicprefix == NULL ? 14 : 12;
      monitoringSymbolsWithinSlot = (ss->monitoringSymbolsWithinSlot->buf[0]<<(sps-8)) | (ss->monitoringSymbolsWithinSlot->buf[1]>>(16-sps));
      rel15->rnti = mac->ra.t_crnti;
      rel15->SubcarrierSpacing = current_DL_BWP->scs;
      break;
    case NR_RNTI_SP_CSI:
      break;
    case NR_RNTI_SI:
      sps=14;
      // for SPS=14 8 MSBs in positions 13 down to 6
      monitoringSymbolsWithinSlot = (ss->monitoringSymbolsWithinSlot->buf[0]<<(sps-8)) | (ss->monitoringSymbolsWithinSlot->buf[1]>>(16-sps));
      rel15->rnti = SI_RNTI; // SI-RNTI - 3GPP TS 38.321 Table 7.1-1: RNTI values
      rel15->SubcarrierSpacing = mac->mib->subCarrierSpacingCommon;
      if(mac->frequency_range == FR2)
        rel15->SubcarrierSpacing = mac->mib->subCarrierSpacingCommon + 2;
      break;
    case NR_RNTI_SFI:
      break;
    case NR_RNTI_INT:
      break;
    case NR_RNTI_TPC_PUSCH:
      break;
    case NR_RNTI_TPC_PUCCH:
      break;
    case NR_RNTI_TPC_SRS:
      break;
    default:
      break;
  }

  for (int i = 0; i < sps; i++) {
    if ((monitoringSymbolsWithinSlot >> (sps - 1 - i)) & 1) {
      rel15->coreset.StartSymbolIndex = i;
      break;
    }
  }
  uint32_t Y = 0;
  if (ss->searchSpaceType->present == NR_SearchSpace__searchSpaceType_PR_ue_Specific)
    Y = get_Y(ss, slot, rel15->rnti);
  fill_dci_search_candidates(ss, rel15, Y);

  #ifdef DEBUG_DCI
    for (int i = 0; i < rel15->num_dci_options; i++) {
      LOG_D(MAC, "[DCI_CONFIG] Configure DCI PDU: rnti_type %d BWPSize %d BWPStart %d rel15->SubcarrierSpacing %d rel15->dci_format %d rel15->dci_length %d sps %d monitoringSymbolsWithinSlot %d \n",
            rnti_type,
            rel15->BWPSize,
            rel15->BWPStart,
            rel15->SubcarrierSpacing,
            rel15->dci_format_options[i],
            rel15->dci_length_options[i],
            sps,
            monitoringSymbolsWithinSlot);
    }
  #endif
  // add DCI
  dl_config->dl_config_list[dl_config->number_pdus].pdu_type = FAPI_NR_DL_CONFIG_TYPE_DCI;
  dl_config->number_pdus += 1;
}

bool is_ss_monitor_occasion(const int frame, const int slot, const int slots_per_frame, const NR_SearchSpace_t *ss)
{
  const int duration = ss->duration ? *ss->duration : 1;
  bool monitor = false;
  int period, offset;
  get_monitoring_period_offset(ss, &period, &offset);
  // The UE monitors PDCCH candidates for search space set ss for 'duration' consecutive slots
  for (int i = 0; i < duration; i++) {
    if (((frame * slots_per_frame + slot - offset - i) % period) == 0) {
      monitor = true;
      break;
    }
  }
  return monitor;
}
bool search_space_monitoring_ocasion_other_si(NR_UE_MAC_INST_t *mac,
                                              const NR_SearchSpace_t *ss,
                                              const int abs_slot,
                                              const int frame,
                                              const int slot,
                                              const int slots_per_frame)
{
  const int duration = ss->duration ? *ss->duration : 1;
  int period, offset;
  get_monitoring_period_offset(ss, &period, &offset);
  for (int i = 0; i < duration; i++) {
    if (((frame * slots_per_frame + slot - offset - i) % period) == 0) {
      int N = mac->ssb_list.nb_tx_ssb;
      // int K = mac->ssb_list.nb_ssb_per_index[mac->mib_ssb];
      int K = 0; // k_th transmitted SSB
      for (int i = 0; i < mac->mib_ssb; i++) {
        if(mac->ssb_list.tx_ssb[i].transmitted)
          K++;
      }
      // numbering current frame and slot in terms of monitoring occasions in window
      int rel_slot = abs_slot - mac->si_SchedInfo.si_window_start;
      int current_monitor_occasion = (rel_slot % period) + (duration * rel_slot / period);
      return current_monitor_occasion % N == K;
    }
  }

  return false;
}
bool is_window_valid(NR_UE_MAC_INST_t *mac, int window_slots, int abs_slot)
{
  if (mac->si_SchedInfo.si_window_start == -1) {
    // out of window
    return false;
  } else if (abs_slot > mac->si_SchedInfo.si_window_start + window_slots) {
    // window expired
    mac->si_SchedInfo.si_window_start = -1;
    return false;
  }
  return true;
}
static bool monitor_dci_for_other_SI(NR_UE_MAC_INST_t *mac,
                                     const NR_SearchSpace_t *ss,
                                     const int si_idx,
                                     const int slots_per_frame,
                                     const int frame,
                                     const int slot)
{
  const int abs_slot = frame * slots_per_frame + slot;

  si_schedinfo_config_t *config = mac->si_SchedInfo.si_SchedInfo_list.array[si_idx];
  int window_slots = 5 << mac->si_SchedInfo.si_WindowLength;
  int x = (config->si_WindowPosition - 1) * window_slots;
  int T = 8 << config->si_Periodicity; // radio frame periodicity
  bool check_valid;
  switch (config->type) {
    case NR_SI_INFO :
      if (mac->si_SchedInfo.si_window_start == -1) {
        if ((frame % T) == (x / slots_per_frame) && (x % slots_per_frame == 0))
          mac->si_SchedInfo.si_window_start = abs_slot; // in terms of absolute slot number
      }
      check_valid = is_window_valid(mac, window_slots, abs_slot);
      if (check_valid && search_space_monitoring_ocasion_other_si(mac, ss, abs_slot, frame, slot, slots_per_frame))
        return true;
      break;
    case NR_SI_INFO_v1700 :
      if (mac->si_SchedInfo.si_window_start == -1) {
        if ((frame % T == floor(x / slots_per_frame)) && (slot == x % slots_per_frame))
          mac->si_SchedInfo.si_window_start = abs_slot;
      }
      if (is_window_valid(mac, window_slots, abs_slot))
        return search_space_monitoring_ocasion_other_si(mac, ss, abs_slot, frame, slot, slots_per_frame);
      break;
    default :
      AssertFatal(false, "Invalid SI-SchedulingInfo case\n");
  }
  return false;


  // // 5.2.2.3.2 in 331
  // const struct NR_SI_SchedulingInfo *si_SchedulingInfo = mac->si_SchedulingInfo;
  // const int abs_slot = frame * slots_per_frame + slot;
  // if (!si_SchedulingInfo)
  //   return false;
  // const int si_window_slots = 5 << si_SchedulingInfo->si_WindowLength;
  // const int abs_slot = frame * slots_per_frame + slot;
  
  //   struct NR_SchedulingInfo *sched_Info = si_SchedulingInfo->schedulingInfoList.list.array[si_idx];
  //   if(mac->si_window_start == -1) {
  //     int x = n * si_window_slots;
  //     int T = 8 << sched_Info->si_Periodicity; // radio frame periodicity
  //     if ((frame % T) == (x / slots_per_frame) &&
  //         (x % slots_per_frame == 0))
  //       mac->si_window_start = abs_slot; // in terms of absolute slot number
  //   }
  //   if (mac->si_window_start == -1) {
  //     // out of window
  //     return false;
  //   }
  //   else if (abs_slot > mac->si_window_start + si_window_slots) {
  //     // window expired
  //     mac->si_window_start = -1;
  //     return false;
  //   }
  //   else {
  //     const int duration = ss->duration ? *ss->duration : 1;
  //     int period, offset;
  //     get_monitoring_period_offset(ss, &period, &offset);
  //     for (int i = 0; i < duration; i++) {
  //       if (((frame * slots_per_frame + slot - offset - i) % period) == 0) {
  //         int N = mac->ssb_list.nb_tx_ssb;
  //         int K = 0; // k_th transmitted SSB
  //         for (int i = 0; i < mac->mib_ssb; i++) {
  //           if(mac->ssb_list.tx_ssb[i].transmitted)
  //             K++;
  //         }
  //         // numbering current frame and slot in terms of monitoring occasions in window
  //         int current_monitor_occasion = ((abs_slot - mac->si_window_start) % period) +
  //                                        (duration * (abs_slot - mac->si_window_start) / period);
  //         if (current_monitor_occasion % N == K)
  //           return true;
  //         else
  //          return false;
  //       }
  //     }
  //   }
  return false;
}


void ue_dci_configuration(NR_UE_MAC_INST_t *mac, fapi_nr_dl_config_request_t *dl_config, const frame_t frame, const int slot)
{
  const NR_UE_DL_BWP_t *current_DL_BWP = &mac->current_DL_BWP;
  int scs = mac->get_sib1 ? get_softmodem_params()->numerology : current_DL_BWP->scs;
  const int slots_per_frame = nr_slots_per_frame[scs];
  if (mac->get_sib1) {
    int ssb_sc_offset_norm;
    if (mac->ssb_subcarrier_offset < 24 && mac->frequency_range == FR1)
      ssb_sc_offset_norm = mac->ssb_subcarrier_offset >> scs;
    else
      ssb_sc_offset_norm = mac->ssb_subcarrier_offset;
    uint16_t ssb_offset_point_a = (mac->ssb_start_subcarrier - ssb_sc_offset_norm) / 12;
    int ssb_start_symbol = get_ssb_start_symbol(mac->nr_band, scs, mac->mib_ssb);
    get_type0_PDCCH_CSS_config_parameters(&mac->type0_PDCCH_CSS_config,
                                          mac->mib_frame,
                                          mac->mib,
                                          slots_per_frame,
                                          ssb_sc_offset_norm,
                                          ssb_start_symbol,
                                          scs,
                                          mac->frequency_range,
                                          mac->nr_band,
                                          mac->mib_ssb,
                                          1, // If the UE is not configured with a periodicity, the UE assumes a periodicity of a half frame
                                          ssb_offset_point_a);
    if(mac->search_space_zero == NULL)
      mac->search_space_zero=calloc(1,sizeof(*mac->search_space_zero));
    if(mac->coreset0 == NULL)
      mac->coreset0 = calloc(1,sizeof(*mac->coreset0));
    fill_coresetZero(mac->coreset0, &mac->type0_PDCCH_CSS_config);
    fill_searchSpaceZero(mac->search_space_zero, slots_per_frame, &mac->type0_PDCCH_CSS_config);
    if (is_ss_monitor_occasion(frame, slot, slots_per_frame, mac->search_space_zero)) {
      LOG_D(NR_MAC, "Monitoring DCI for SIB1 in frame %d slot %d\n", frame, slot);
      config_dci_pdu(mac, dl_config, NR_RNTI_SI, slot, mac->search_space_zero);
    }
  }

  for (int i = 0; i < MAX_SI_GROUPS; i++) {
    if (!mac->get_otherSI[i])
      continue;
    // If searchSpaceOtherSystemInformation is set to zero,
    // PDCCH monitoring occasions for SI message reception in SI-window
    // are same as PDCCH monitoring occasions for SIB1
    const NR_SearchSpace_t *ss = mac->otherSI_SS ? mac->otherSI_SS : mac->search_space_zero;
    LOG_E(NR_MAC, "mac->get_otherSI_SS : %ld\n", ss->searchSpaceId); //Abdallah Abou Hasna
    // TODO configure SI-window
    if (monitor_dci_for_other_SI(mac, ss, i,slots_per_frame, frame, slot)) {
      LOG_D(NR_MAC, "Monitoring DCI for other SIs in frame %d slot %d\n", frame, slot);
      config_dci_pdu(mac, dl_config, NR_RNTI_SI, slot, ss);
    }
  }
  if (mac->state == UE_PERFORMING_RA &&
      mac->ra.ra_state >= WAIT_RAR) {
    // if RA is ongoing use RA search space
    if (is_ss_monitor_occasion(frame, slot, slots_per_frame, mac->ra_SS)) {
      int rnti_type = mac->ra.ra_state == WAIT_RAR ? NR_RNTI_RA : NR_RNTI_TC;
      config_dci_pdu(mac, dl_config, rnti_type, slot, mac->ra_SS);
    }
  }
  else if (mac->state == UE_CONNECTED) {
    bool found = false;
    for (int i = 0; i < FAPI_NR_MAX_SS; i++) {
      if (mac->BWP_searchspaces[i] != NULL) {
        found = true;
        NR_SearchSpace_t *ss = mac->BWP_searchspaces[i];
        if (is_ss_monitor_occasion(frame, slot, slots_per_frame, ss))
          config_dci_pdu(mac, dl_config, NR_RNTI_C, slot, ss);
      }
    }
    if (!found && mac->ra_SS) {
      // If the UE has not been provided a Type3-PDCCH CSS set or a USS set and
      // the UE has received a C-RNTI and has been provided a Type1-PDCCH CSS set,
      // the UE monitors PDCCH candidates for DCI format 0_0 and DCI format 1_0
      // with CRC scrambled by the C-RNTI in the Type1-PDCCH CSS set
      if (is_ss_monitor_occasion(frame, slot, slots_per_frame, mac->ra_SS))
        config_dci_pdu(mac, dl_config, NR_RNTI_C, slot, mac->ra_SS);
    }
  }
}
