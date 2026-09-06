// 语音助手页：安卓端通过系统输入法麦克风语音输入（input 聚焦后由输入法识别文字填入），
// 文字确认/停顿后调用后端 /api/conv 多轮引导对话，自动报价。
const api = require('../../utils/api.js')

const PARAM_CN = {
  date: '日期', cust_type: '客户类型', room_type: '房型', category: '房型分类',
  nights: '晚数', rooms: '间数', venue_id: '会场', people: '人数', layout: '布局',
  need_equip: '设备需求', is_full_day: '时段', hours: '小时数',
  need_rehearsal: '是否需要彩排', need_lighting: '灯光', need_screen: '大屏',
  catering_amount: '餐饮标准'
}

Page({
  data: {
    messages: [],
    inputText: '',
    typing: false,
    listening: false,   // 输入法语音待命（已聚焦输入框）
    autoFocus: false,
    statusText: '点击 🎤 聚焦输入框，再用输入法麦克风说话；或直接输入文字',
    statusType: '',
    scrollAnchor: 'bottom'
  },

  conv: null,           // 后端多轮对话状态
  autoSend: false,      // 识别/输入后自动发送
  _id: 0,
  _timer: null,

  onLoad() {
    this.pushMsg('ai', '您好，我是龙顺语音助手 🤝\n可以直接说话或输入，例如：\n· “帮我订一间大床房”\n· “帮我订个会场”\n· “今天订单多少”')
    // 进入即聚焦输入框，引导用户用输入法麦克风
    this.setData({ autoFocus: true, listening: true })
  },

  onUnload() {
    if (this._timer) clearTimeout(this._timer)
  },

  setStatus(t, type) {
    this.setData({ statusText: t, statusType: type || '' })
  },

  pushMsg(role, text, extra) {
    const id = ++this._id
    const msg = Object.assign({ id, role, text: text || '', cards: [], options: [] }, extra || {})
    const messages = this.data.messages.concat(msg)
    this.setData({ messages, scrollAnchor: 'm' + id })
    return id
  },

  updateLastMsg(id, patch) {
    const messages = this.data.messages.map(m => m.id === id ? Object.assign({}, m, patch) : m)
    this.setData({ messages })
  },

  // 点击 🎤：聚焦输入框，引导使用输入法麦克风（系统级语音输入，文字直接填入）
  toggleMic() {
    const on = !this.data.listening
    this.setData({ listening: on, autoFocus: on })
    if (on) {
      this.setStatus('🎤 已聚焦输入框：请点键盘上的麦克风 🎙 说话，文字会自动填入，说完好点「发送」或回车', 'ok')
    } else {
      this.setStatus('语音输入已关闭，可直接输入文字或再次点击 🎤')
    }
  },

  onInput(e) {
    const v = e.detail.value
    this.setData({ inputText: v })
    // 输入法语音持续填入，停顿即自动发送（模拟“说完即问”）
    if (this._timer) clearTimeout(this._timer)
    if (this.autoSend && v.trim()) {
      this._timer = setTimeout(() => {
        if (this.data.inputText.trim()) this.send()
      }, 1200)
    }
  },

  onConfirm() {
    // 键盘“发送”按钮（confirm-type=send）即直接发送
    this.send()
  },

  useChip(e) {
    this.setData({ inputText: e.currentTarget.dataset.text })
    this.send()
  },

  sendText() { this.send() },

  async send() {
    const q = (this.data.inputText || '').trim()
    if (!q) return
    this.autoSend = false
    if (this._timer) { clearTimeout(this._timer); this._timer = null }
    this.setData({ inputText: '', listening: false, autoFocus: false })
    const mid = this.pushMsg('me', q)
    this.setData({ typing: true })
    this.setStatus('正在思考…')
    try {
      const payload = { q }
      if (this.conv) {
        payload.intent = this.conv.intent
        ['date', 'cust_type', 'room_type', 'category', 'building', 'nights', 'rooms',
         'venue_id', 'people', 'layout', 'need_equip', 'is_full_day', 'hours',
         'need_rehearsal', 'need_lighting', 'need_screen', 'catering_amount']
          .forEach(k => { if (this.conv[k] != null && this.conv[k] !== '') payload[k] = this.conv[k] })
      }
      const c = await api.conv(payload)
      this.setData({ typing: false })

      if (c && c.conv) {
        if (c.done) {
          if (c.no_match) { this.reask(c.retry_param, c.answer); return }
          this.conv = null
          const book = this.buildBookParam(c)
          this.pushMsg('ai', (c.answer || '') + (c.note ? '\n💡 ' + c.note : ''), { cards: c.cards || [], book })
          this.setStatus('报价方案已生成，可点「去下单确认」或继续提问')
          return
        }
        // 缺参数：更新多轮状态，追问
        this.conv = {
          intent: c.intent, date: c.date || '', cust_type: c.cust_type || '',
          room_type: c.room_type || '', category: c.category || '', building: c.building || '',
          nights: c.nights || 0, rooms: c.rooms || 0,
          venue_id: c.venue_id || 0, people: c.people || 0, layout: c.layout || '',
          need_equip: c.need_equip || '',
          is_full_day: (c.is_full_day == null ? -1 : c.is_full_day), hours: c.hours || 0,
          need_rehearsal: c.need_rehearsal == null ? -1 : c.need_rehearsal,
          need_lighting: c.need_lighting == null ? -1 : c.need_lighting,
          need_screen: c.need_screen == null ? -1 : c.need_screen,
          catering_amount: c.catering_amount == null ? -1 : c.catering_amount
        }
        const keys = c.intent === 'room'
          ? ['date', 'cust_type', 'room_type', 'category', 'nights', 'rooms']
          : ['date', 'cust_type', 'venue_id', 'people', 'layout', 'need_equip', 'is_full_day', 'hours', 'need_rehearsal', 'need_lighting', 'need_screen', 'catering_amount']
        const got = keys.filter(k => {
          const v = this.conv[k]
          if (['date', 'cust_type', 'room_type', 'category', 'layout', 'need_equip'].includes(k)) return !!v
          if (['is_full_day', 'need_rehearsal', 'need_lighting', 'need_screen', 'caterering_amount', 'catering_amount'].includes(k)) return v >= 0
          return v > 0
        })
        this.pushMsg('ai', c.question || c.answer, { cards: c.cards || [], options: c.options || [] })
        this.autoSend = true
        this.setData({ listening: true, autoFocus: true })
        this.setStatus('🔊 第' + (got.length + 1) + '/' + keys.length + '问：请说话或点选项回复（输入法语音说完会自动发送）', 'ok')
        return
      }

      // 非引导：自然语言查询
      const r = await api.request('/api/query?q=' + encodeURIComponent(q))
      this.pushMsg('ai', r.answer || '（无返回）', { cards: r.items ? (r.items.map(it => ({ title: it.title, sub: it.sub, price: it.price }))) : [] })
      this.setStatus('可继续提问或点 🎤')
    } catch (e) {
      this.setData({ typing: false })
      this.pushMsg('ai', '⚠️ 请求失败：' + (e.errMsg || e.message || '网络错误'))
      this.setStatus('⚠️ 连接服务端失败，请确认服务已启动并允许非正式域名', 'err')
    }
  },

  // 从报价结果构造确认下单参数
  buildBookParam(c) {
    const det = []
    if (c.category) det.push('早餐 ' + c.category)
    if (c.building) det.push('楼栋 ' + c.building)
    if (c.type === 'room') {
      if (c.nights) det.push(c.nights + '晚')
      if (c.rooms) det.push(c.rooms + '间')
    } else if (c.type === 'venue') {
      if (c.people) det.push(c.people + '人')
      if (c.layout) det.push('布局 ' + c.layout)
    }
    return {
      type: c.type || 'room',
      item: c.room_type || '',
      amount: c.amount || '',
      date: c.date || '',
      remark: det.length ? det.join(' · ') : ''
    }
  },

  // 跳转订单确认/下单页
  goBook(e) {
    const b = e.currentTarget.dataset.book || {}
    const qs = []
    if (b.type) qs.push('type=' + b.type)
    if (b.item) qs.push('item=' + encodeURIComponent(b.item))
    if (b.amount != null && b.amount !== '') qs.push('amount=' + b.amount)
    if (b.date) qs.push('date=' + b.date)
    if (b.remark) qs.push('remark=' + encodeURIComponent(b.remark))
    wx.navigateTo({ url: '/pages/booking/booking?' + qs.join('&') })
  },

  reask(param, msg) {
    if (this.conv && param) {
      if (this.conv[param] != null) {
        if (typeof this.conv[param] === 'number') this.conv[param] = 0; else this.conv[param] = ''
      }
      if (param === 'room_type') this.conv.category = ''
    }
    this.pushMsg('ai', '⚠️ ' + (msg || '暂无匹配方案，请调整条件'))
    this.autoSend = true
    this.setData({ listening: true, autoFocus: true })
    this.setStatus('⚠️ ' + (msg || '当前条件无满足方案，请重新选择'))
  }
})
