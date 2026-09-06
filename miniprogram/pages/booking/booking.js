// 在线预订：客房 / 会场 下单
const api = require('../../utils/api')

Page({
  data: {
    type: 'room',
    item: '',
    customer: '',
    phone: '',
    date: '',
    dateEnd: '',
    days: 1,
    session: 'half',
    amount: '',
    remark: '',
    submitting: false
  },

  onLoad(options) {
    const type = options.type === 'venue' ? 'venue' : 'room'
    const item = options.item ? decodeURIComponent(options.item) : ''
    const amount = options.amount || ''
    const date = options.date || this.today()
    const dateEnd = options.date ? this.addDays(options.date, 1) : this.addDays(date, 1)
    const remark = options.remark ? decodeURIComponent(options.remark) : ''
    this.setData({ type, item, amount, date, dateEnd, days: 1, remark })
  },

  today() {
    const d = new Date()
    const m = String(d.getMonth() + 1).padStart(2, '0')
    const day = String(d.getDate()).padStart(2, '0')
    return d.getFullYear() + '-' + m + '-' + day
  },

  addDays(iso, n) {
    const d = new Date(iso + 'T00:00:00')
    d.setDate(d.getDate() + n)
    const m = String(d.getMonth() + 1).padStart(2, '0')
    const day = String(d.getDate()).padStart(2, '0')
    return d.getFullYear() + '-' + m + '-' + day
  },

  onTypeChange(e) {
    this.setData({ type: e.detail.value })
  },

  onSessionChange(e) {
    this.setData({ session: e.detail.value })
  },

  onInput(e) {
    const key = e.currentTarget.dataset.key
    this.setData({ [key]: e.detail.value })
  },

  onDateChange(e) {
    const date = e.detail.value
    const dateEnd = this.addDays(date, 1)
    this.setData({ date, dateEnd, days: 1 })
  },

  onDateEndChange(e) {
    const dateEnd = e.detail.value
    const days = this.daysBetween(this.data.date, dateEnd)
    this.setData({ dateEnd, days })
  },

  daysBetween(a, b) {
    const n = Math.round((new Date(b + 'T00:00:00') - new Date(a + 'T00:00:00')) / 86400000)
    return n >= 1 ? n : 1
  },

  onSubmit() {
    const { customer, phone, type, item, date, dateEnd, days, session, amount, remark } = this.data
    if (!customer.trim()) return wx.showToast({ title: '请填写姓名', icon: 'none' })
    if (!phone.trim() || phone.trim().length < 6)
      return wx.showToast({ title: '请填写正确手机号', icon: 'none' })
    if (!item.trim()) return wx.showToast({ title: '请选择房型/会场', icon: 'none' })
    if (!amount || Number(amount) <= 0) return wx.showToast({ title: '请填写金额', icon: 'none' })

    this.setData({ submitting: true })
    const payload = {
      customer: customer.trim(),
      phone: phone.trim(),
      type,
      date,
      check_in: date,
      check_out: dateEnd,
      nights: days,
      amount: Number(amount),
      remark: remark.trim()
    }
    if (type === 'room') {
      payload.room_type = item.trim()
    } else {
      payload.venue = item.trim()
      payload.session = session
    }

    api
      .createBooking(payload)
      .then((res) => {
        this.setData({ submitting: false })
        if (res.code !== 0) {
          return wx.showToast({ title: res.msg || '提交失败', icon: 'none' })
        }
        const orderId = res.order && res.order.id
        wx.redirectTo({
          url:
            '/pages/pay/pay?orderId=' +
            encodeURIComponent(orderId) +
            '&amount=' +
            amount
        })
      })
      .catch(() => {
        this.setData({ submitting: false })
        wx.showToast({ title: '网络错误，请检查服务端', icon: 'none' })
      })
  }
})
